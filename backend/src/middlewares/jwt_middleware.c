/**
 * @file jwt_middleware.c
 * @brief JWT 身份认证与 Token 状态校验中间件实现
 *
 * 实现了基于 HS256 算法的 JWT 签名解析、时间有效性验证、白名单路由绕过、
 * 以及基于数据库 token_version 字段的 Token 吊销有效性比对。
 */

#include "middlewares/jwt_middleware.h"
#include "common/db.h"
#include "common/jwt.h"
#include "config/secret.h"
#include "csilk/core/crypto.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

/* Per-process short-lived cache of token_version lookups. Each authenticated
 * request otherwise runs a single-row DB query; under load this is the hottest
 * read on the users table. A 5 s TTL bounds staleness after a password change
 * or full-session invalidation while cutting most of the queries.
 *
 * Ring eviction: when the table is full, new entries overwrite the slot
 * selected by monotonic-time hash. An evicted user will miss and re-query the
 * DB on their next request, so the worst case is one extra query, never a
 * wrong answer. */
#define TOKEN_VERSION_CACHE_TTL_SEC 5
#define TOKEN_VERSION_CACHE_MAX_USERS 128

struct tv_cache_entry {
    int64_t user_id;
    int     db_version;
    int     found; /* 0 = user row absent (treat as invalid), 1 = present */
    int64_t expire_mono_ms;
};

static struct tv_cache_entry g_tv_cache[TOKEN_VERSION_CACHE_MAX_USERS];
static size_t                g_tv_cache_count = 0;
static pthread_mutex_t       g_tv_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief 校验 JWT 载荷中的版本号与数据库中用户的最新 token_version 是否一致
 *
 * 用于支持用户登出、修改密码或踢出全部会话时的 Token 批量作废。
 *
 * @param[in,out] c HTTP 请求上下文指针（用于获取上下文中的 jwt_payload）
 * @param[in] user_id 当前请求的用户 ID
 *
 * @return int 状态码
 * @retval 0 版本号一致，Token 有效
 * @retval -1 版本号不匹配、用户不存在或载荷解析失败，Token 已失效
 *
 * @note 内部函数，优先命中 5 s 进程内缓存，未命中时回退到数据库单行查询。
 */
static int
jwt_validate_token_version(csilk_ctx_t* c, int64_t user_id)
{
    csilk_json_t* root = (csilk_json_t*)csilk_get(c, "jwt_payload");
    if (!root) {
        return -1;
    }
    int jwt_version = (int)csilk_json_get_number(root, "v");

    int64_t now_ms;
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        now_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }

    /*
     * Fast path: only a cache entry whose stored version matches the token is
     * trusted. A mismatch must NOT short-circuit to a rejection — the entry can
     * predate a password change / logout that bumped users.token_version, so a
     * token carrying a newer version is re-checked against the DB below. Without
     * this, every freshly issued token is refused for up to
     * TOKEN_VERSION_CACHE_TTL_SEC after a version bump (e.g. re-login right
     * after a password change would be rejected with "Token invalidated").
     */
    pthread_mutex_lock(&g_tv_cache_mutex);
    for (size_t i = 0; i < g_tv_cache_count; i++) {
        struct tv_cache_entry* e = &g_tv_cache[i];
        if (e->user_id == user_id && e->expire_mono_ms > now_ms && e->found &&
            jwt_version == e->db_version) {
            pthread_mutex_unlock(&g_tv_cache_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_tv_cache_mutex);

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    csilk_db_pool_t* pool = db_get_pool();
    const char*      params[] = {uid_str, NULL};
    csilk_json_t*    row =
        csilk_db_query_param_json(pool, "SELECT token_version FROM users WHERE id=?", params);
    int db_version, found;
    if (!row || csilk_json_array_size(row) == 0) {
        if (row) {
            csilk_json_free(row);
        }
        found = 0;
        db_version = 0;
    } else {
        found = 1;
        db_version = (int)db_get_int(csilk_json_array_get(row, 0), "token_version");
        csilk_json_free(row);
    }

    /* Store into cache, refreshing an existing entry for this user in place so a
     * re-queried version does not get shadowed by a stale duplicate. */
    pthread_mutex_lock(&g_tv_cache_mutex);
    size_t slot = g_tv_cache_count;
    for (size_t i = 0; i < g_tv_cache_count; i++) {
        if (g_tv_cache[i].user_id == user_id) {
            slot = i;
            break;
        }
    }
    if (slot == g_tv_cache_count) {
        if (g_tv_cache_count < TOKEN_VERSION_CACHE_MAX_USERS) {
            g_tv_cache_count++;
        } else {
            slot = now_ms % TOKEN_VERSION_CACHE_MAX_USERS;
        }
    }
    struct tv_cache_entry* e = &g_tv_cache[slot];
    e->user_id = user_id;
    e->db_version = db_version;
    e->found = found;
    e->expire_mono_ms = now_ms + TOKEN_VERSION_CACHE_TTL_SEC * 1000;
    pthread_mutex_unlock(&g_tv_cache_mutex);

    return (found && jwt_version == db_version) ? 0 : -1;
}

/**
 * @brief JWT 身份鉴权中间件入口函数
 *
 * @param[in,out] c HTTP 上下文对象指针
 */
void
jwt_middleware_wrapper(csilk_ctx_t* c)
{
    if (!c) {
        return;
    }

    const char* path = csilk_get_path(c);
    if (path &&
        (strcmp(path, "/api/auth/login") == 0 || strcmp(path, "/api/auth/register") == 0 ||
         strcmp(path, "/api/system/status") == 0 || strcmp(path, "/api/system/setup") == 0 ||
         strcmp(path, "/api/auth/public-key") == 0 ||
         strcmp(path, "/api/auth/2fa/verify-login") == 0 ||
         strcmp(path, "/api/auth/oauth/providers") == 0 ||
         strcmp(path, "/api/auth/oauth/callback") == 0)) {
        csilk_next(c);
        return;
    }

    const char* secret = config_secret_get("JWT_SECRET", NULL, 0);
    if (!secret || secret[0] == '\0') {
        csilk_json_error(c,
                         CSILK_STATUS_INTERNAL_SERVER_ERROR,
                         "JWT_SECRET is required and must be properly configured");
        csilk_abort(c);
        return;
    }

    const char* auth_header = csilk_get_header(c, "Authorization");
    if (!auth_header || strncmp(auth_header, "Bearer ", 7) != 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Bearer token required");
        csilk_abort(c);
        return;
    }
    const char* token = auth_header + 7;

    csilk_jwt_options_t opts = {
        .algorithm = CSILK_JWT_HS256,
        .flags = CSILK_JWT_REQUIRE_EXP,
        .leeway_sec = 0,
    };
    csilk_json_t* payload = csilk_jwt_verify_options(c, token, secret, strlen(secret), &opts);
    if (!payload) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Invalid or expired token");
        csilk_abort(c);
        return;
    }

    csilk_set_ex(c, "jwt_payload", payload, (void (*)(void*))csilk_json_free);

    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Invalid user in token claims");
        csilk_abort(c);
        return;
    }
    if (jwt_validate_token_version(c, user_id) != 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Token invalidated — please log in again");
        csilk_abort(c);
        return;
    }

    csilk_next(c);
}
