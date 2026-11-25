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
#include "csilk/crypto/crypto.h"
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
jwt_validate_version_num(int64_t user_id, int jwt_version)
{
    int64_t now_ms;
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        now_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }

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
    csilk_json_t*    row = csilk_db_query_param_json(
        pool,
        "SELECT token_version, COALESCE(disabled, 0) AS disabled FROM users WHERE id=?",
        params);
    int db_version, found;
    if (!row || csilk_json_array_size(row) == 0) {
        if (row) {
            csilk_json_free(row);
        }
        found = 0;
        db_version = 0;
    } else {
        csilk_json_t* r = csilk_json_array_get(row, 0);
        int           disabled = db_get_bool(r, "disabled");
        found = (disabled == 0) ? 1 : 0;
        db_version = (int)db_get_int(r, "token_version");
        csilk_json_free(row);
    }

    pthread_mutex_lock(&g_tv_cache_mutex);
    size_t slot = g_tv_cache_count;
    size_t expired_slot = SIZE_MAX;
    for (size_t i = 0; i < g_tv_cache_count; i++) {
        if (g_tv_cache[i].user_id == user_id) {
            slot = i;
            break;
        }
        if (expired_slot == SIZE_MAX &&
            (g_tv_cache[i].user_id <= 0 || g_tv_cache[i].expire_mono_ms <= now_ms)) {
            expired_slot = i;
        }
    }
    if (slot == g_tv_cache_count) {
        if (g_tv_cache_count < TOKEN_VERSION_CACHE_MAX_USERS) {
            g_tv_cache_count++;
        } else if (expired_slot != SIZE_MAX) {
            slot = expired_slot;
        } else {
            slot = (size_t)(now_ms % TOKEN_VERSION_CACHE_MAX_USERS);
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

void
jwt_middleware_invalidate_user(int64_t user_id)
{
    if (user_id <= 0) {
        return;
    }
    pthread_mutex_lock(&g_tv_cache_mutex);
    for (size_t i = 0; i < g_tv_cache_count; i++) {
        if (g_tv_cache[i].user_id == user_id) {
            g_tv_cache[i].user_id = 0;
            g_tv_cache[i].db_version = 0;
            g_tv_cache[i].found = 0;
            g_tv_cache[i].expire_mono_ms = 0;
        }
    }
    pthread_mutex_unlock(&g_tv_cache_mutex);
}

static bool
is_im_bind_auth_path(const char* path)
{
    if (!path) {
        return false;
    }
    size_t plen = strlen(path);
    /* 严格匹配单段平台标识: /api/im/<platform>/bind-auth (禁止 '..' 或多段 '/') */
    if (plen <= 18 || strncmp(path, "/api/im/", 8) != 0 ||
        strncmp(path + plen - 10, "/bind-auth", 10) != 0) {
        return false;
    }
    size_t seg_len = plen - 18;
    if (seg_len == 0 || seg_len > 32) {
        return false;
    }
    for (size_t i = 0; i < seg_len; i++) {
        unsigned char ch = (unsigned char)path[8 + i];
        bool          ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static const char* s_jwt_exclude_paths[] = {"/api/auth/login",
                                            "/api/auth/register",
                                            "/api/system/status",
                                            "/api/system/setup",
                                            "/api/auth/public-key",
                                            "/api/auth/2fa/verify-login",
                                            "/api/auth/oauth/providers",
                                            "/api/auth/oauth/callback",
                                            NULL};

static bool
jwt_authenticate_ctx(csilk_ctx_t* c, int64_t* out_user_id)
{
    if (!c) {
        return false;
    }

    const char* path = csilk_get_path(c);
    if (path) {
        for (int i = 0; s_jwt_exclude_paths[i]; i++) {
            if (strcmp(path, s_jwt_exclude_paths[i]) == 0) {
                if (out_user_id) {
                    *out_user_id = 0;
                }
                return true;
            }
        }
        /* P2-A: IM 自助绑定端点 /api/im/{platform}/bind-auth 无 JWT（IM 用户
         * 未登录），靠限流 + 抢占防护保护。严格校验单段 platform 字符集，防路径穿越。 */
        if (is_im_bind_auth_path(path)) {
            if (out_user_id) {
                *out_user_id = 0;
            }
            return true;
        }
        /* 网关内部路由 /api/gateway/ 由各 handler 内 gateway_auth_check() 验证 X-Gateway-Token */
        if (strncmp(path, "/api/gateway/", 13) == 0 && strstr(path, "..") == NULL) {
            if (out_user_id) {
                *out_user_id = 0;
            }
            return true;
        }
    }

    const char* secret = config_secret_get("JWT_SECRET", NULL, 0);
    if (!secret || secret[0] == '\0') {
        csilk_json_error(c,
                         CSILK_STATUS_INTERNAL_SERVER_ERROR,
                         "JWT_SECRET is required and must be properly configured");
        csilk_abort(c);
        return false;
    }

    bool is_mcp_path = (path && strcmp(path, "/mcp") == 0);

    const char* auth_header = csilk_get_header(c, "Authorization");
    if (!auth_header || strncmp(auth_header, "Bearer ", 7) != 0) {
        if (is_mcp_path) {
            /* Pass through unauthenticated so mcp_endpoint_handler returns JSON-RPC -32001 */
            if (out_user_id) {
                *out_user_id = 0;
            }
            return true;
        }
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Bearer token required");
        csilk_abort(c);
        return false;
    }
    const char* token = auth_header + 7;

    csilk_jwt_options_t opts = {
        .algorithm = CSILK_JWT_HS256,
        .flags = CSILK_JWT_REQUIRE_EXP,
        .leeway_sec = 0,
    };
    csilk_json_t* payload = csilk_jwt_verify_options(c, token, secret, strlen(secret), &opts);
    if (!payload) {
        if (is_mcp_path) {
            if (out_user_id) {
                *out_user_id = 0;
            }
            return true;
        }
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Invalid or expired token");
        csilk_abort(c);
        return false;
    }

    const char* token_type = csilk_json_get_string(payload, "type");
    if (token_type && strcmp(token_type, "2fa_pending") == 0) {
        if (!path || strcmp(path, "/api/auth/2fa/verify-login") != 0) {
            csilk_json_free(payload);
            if (is_mcp_path) {
                if (out_user_id) {
                    *out_user_id = 0;
                }
                return true;
            }
            csilk_json_error(
                c,
                CSILK_STATUS_UNAUTHORIZED,
                "2FA verification required: temporary token cannot access protected endpoints");
            csilk_abort(c);
            return false;
        }
    }
    /* IM 绑定码 (type=im_bind) 仅放行安全 /api/im/ 路径（禁止 '..' 穿越与 /api/im/instances 管理端点） */
    if (token_type && strcmp(token_type, "im_bind") == 0) {
        if (!path || strncmp(path, "/api/im/", 8) != 0 ||
            strncmp(path, "/api/im/instances", 17) == 0 || strstr(path, "..") != NULL) {
            csilk_json_free(payload);
            csilk_json_error(c,
                             CSILK_STATUS_UNAUTHORIZED,
                             "IM bind token is only valid for /api/im/* endpoints");
            csilk_abort(c);
            return false;
        }
    }

    const csilk_json_t* sub_claim = csilk_json_get(payload, "sub");
    const csilk_json_t* v_claim = csilk_json_get(payload, "v");
    if (!sub_claim || !csilk_json_is_number(sub_claim) || !v_claim ||
        !csilk_json_is_number(v_claim)) {
        csilk_json_free(payload);
        if (is_mcp_path) {
            if (out_user_id) {
                *out_user_id = 0;
            }
            return true;
        }
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Invalid token claims");
        csilk_abort(c);
        return false;
    }

    int64_t user_id = (int64_t)csilk_json_get_number(payload, "sub");
    int     jwt_v = (int)csilk_json_get_number(payload, "v");
    if (user_id <= 0 || jwt_v < 0) {
        csilk_json_free(payload);
        if (is_mcp_path) {
            if (out_user_id) {
                *out_user_id = 0;
            }
            return true;
        }
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Invalid user in token claims");
        csilk_abort(c);
        return false;
    }

    /* Validate token version against DB / cache BEFORE context attachment */
    if (jwt_validate_version_num(user_id, jwt_v) != 0) {
        csilk_json_free(payload);
        if (is_mcp_path) {
            /* Token revoked/invalidated on /mcp: pass through unauthenticated without context payload */
            if (out_user_id) {
                *out_user_id = 0;
            }
            return true;
        }
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Token invalidated — please log in again");
        csilk_abort(c);
        return false;
    }

    /* All checks passed: attach payload to context for authenticated requests */
    csilk_set_ex(c, "jwt_payload", payload, (void (*)(void*))csilk_json_free);
    if (out_user_id) {
        *out_user_id = user_id;
    }
    return true;
}

/**
 * @brief JWT 身份鉴权中间件入口函数
 *
 * @param[in,out] c HTTP 上下文对象指针
 */
void
jwt_middleware_wrapper(csilk_ctx_t* c)
{
    if (jwt_authenticate_ctx(c, NULL)) {
        csilk_next(c);
    }
}

void
jwt_admin_middleware_wrapper(csilk_ctx_t* c)
{
    int64_t user_id = 0;
    if (!jwt_authenticate_ctx(c, &user_id)) {
        return;
    }
    if (user_id <= 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Authentication required");
        csilk_abort(c);
        return;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rows = csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(role, 'user') AS role FROM users WHERE id=? AND COALESCE(disabled, 0)=0",
        (const char*[]){uid_str, NULL});
    bool is_admin = false;
    if (rows && csilk_json_array_size(rows) > 0) {
        const char* role = csilk_json_get_string(csilk_json_array_get(rows, 0), "role");
        is_admin = (role && strcmp(role, "admin") == 0);
    }
    if (rows) {
        csilk_json_free(rows);
    }
    if (!is_admin) {
        csilk_json_error(c, CSILK_STATUS_FORBIDDEN, "Admin role required");
        csilk_abort(c);
        return;
    }

    csilk_next(c);
}
