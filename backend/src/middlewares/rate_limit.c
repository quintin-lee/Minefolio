/**
 * @file rate_limit.c
 * @brief 认证相关敏感接口及 /mcp 端点限流中间件实现
 *
 * 采用基于单调时钟 (CLOCK_MONOTONIC) 的按实体（IP+路径 / 用户或未认证 IP）
 * 聚合分桶滑动窗口算法：
 *  - 单一攻击源的高频洪泛仅占用 1 个分桶，避免固定事件环形缓冲区被大量事件刷满覆盖；
 *  - 淘汰时优先清理已过期的陈旧分桶，表满时优先淘汰最低频/最久未活跃的低风险分桶，
 *    保护活跃高危限流状态不被驱逐。
 */

#include "middlewares/rate_limit.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/jwt.h"
#include "csilk/core/ctx/response.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define RATE_LIMIT_WINDOW_SEC 60
#define RATE_LIMIT_MAX_REQS 10
#define AUTH_BUCKET_CAP 2048

typedef struct {
    bool    active;
    char    path[64];
    char    ip[64];
    int64_t ts[RATE_LIMIT_MAX_REQS]; /**< 精确滑动窗口时间戳槽位 */
    int     head;
    int     count;
    int64_t last_seen;
} auth_bucket_t;

static auth_bucket_t   auth_buckets[AUTH_BUCKET_CAP];
static pthread_mutex_t g_rate_limit_mutex = PTHREAD_MUTEX_INITIALIZER;

static int64_t
monotonic_now_sec(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (int64_t)time(NULL);
    }
    return (int64_t)ts.tv_sec;
}

static bool
is_im_bind_auth_path(const char* path)
{
    if (!path) {
        return false;
    }
    size_t plen = strlen(path);
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

static void
auth_bucket_prune(auth_bucket_t* b, int64_t now)
{
    while (b->count > 0 && (now - b->ts[b->head]) > RATE_LIMIT_WINDOW_SEC) {
        b->head = (b->head + 1) % RATE_LIMIT_MAX_REQS;
        b->count--;
    }
}

static auth_bucket_t*
auth_find_or_alloc_bucket(const char* limit_path, const char* safe_ip, int64_t now)
{
    int empty_idx = -1;
    int victim_idx = 0;

    for (int i = 0; i < AUTH_BUCKET_CAP; i++) {
        auth_bucket_t* b = &auth_buckets[i];
        if (!b->active) {
            if (empty_idx < 0) {
                empty_idx = i;
            }
            continue;
        }

        auth_bucket_prune(b, now);
        if (b->count == 0 && (now - b->last_seen) > RATE_LIMIT_WINDOW_SEC) {
            b->active = false;
            if (empty_idx < 0) {
                empty_idx = i;
            }
            continue;
        }

        if (strncmp(b->path, limit_path, sizeof(b->path) - 1) == 0 &&
            strncmp(b->ip, safe_ip, sizeof(b->ip) - 1) == 0) {
            return b;
        }

        /* 若表满，优先淘汰未达限流阈值且计数最低、最久未活跃的分桶，保留活跃封禁态 */
        auth_bucket_t* v = &auth_buckets[victim_idx];
        if (!v->active) {
            victim_idx = i;
        } else {
            bool b_blocked = (b->count >= RATE_LIMIT_MAX_REQS);
            bool v_blocked = (v->count >= RATE_LIMIT_MAX_REQS);
            if ((!b_blocked && v_blocked) ||
                (b_blocked == v_blocked &&
                 (b->count < v->count || (b->count == v->count && b->last_seen < v->last_seen)))) {
                victim_idx = i;
            }
        }
    }

    int            slot = (empty_idx >= 0) ? empty_idx : victim_idx;
    auth_bucket_t* target = &auth_buckets[slot];
    memset(target, 0, sizeof(*target));
    target->active = true;
    snprintf(target->path, sizeof(target->path), "%s", limit_path);
    snprintf(target->ip, sizeof(target->ip), "%s", safe_ip);
    target->last_seen = now;
    return target;
}

void
rate_limit_auth_middleware(csilk_ctx_t* c)
{
    const char* path = csilk_get_path(c);
    if (!path) {
        csilk_next(c);
        return;
    }

    bool        is_im_bind = is_im_bind_auth_path(path);
    const char* limit_path = is_im_bind ? "/api/im/bind-auth" : path;

    if (!is_im_bind && strcmp(path, "/api/auth/login") != 0 &&
        strcmp(path, "/api/auth/register") != 0 && strcmp(path, "/api/system/setup") != 0 &&
        strcmp(path, "/api/auth/2fa/verify-login") != 0 &&
        strcmp(path, "/api/auth/oauth/callback") != 0) {
        csilk_next(c);
        return;
    }

    const char* ip = csilk_get_client_ip(c);
    const char* safe_ip = (ip && ip[0]) ? ip : "127.0.0.1";
    int64_t     now = monotonic_now_sec();

    pthread_mutex_lock(&g_rate_limit_mutex);
    auth_bucket_t* b = auth_find_or_alloc_bucket(limit_path, safe_ip, now);
    b->last_seen = now;

    if (b->count >= RATE_LIMIT_MAX_REQS) {
        pthread_mutex_unlock(&g_rate_limit_mutex);
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_number(resp, "code", 1004);
        csilk_json_add_string(resp, "message", "请求过于频繁，请稍后再试");
        csilk_set_header(c, "Retry-After", "60");
        csilk_json(c, CSILK_STATUS_TOO_MANY_REQUESTS, resp);
        csilk_abort(c);
        return;
    }

    int tail = (b->head + b->count) % RATE_LIMIT_MAX_REQS;
    b->ts[tail] = now;
    b->count++;
    pthread_mutex_unlock(&g_rate_limit_mutex);

    csilk_next(c);
}

/* ================================================================== */
/* /mcp 每用户 & 未认证 IP 限流（按实体分桶滑动子窗口，防洪水驱逐）  */
/* ================================================================== */

#define MCP_RATE_MAX_PER_MIN 120
#define MCP_UNAUTH_RATE_MAX_PER_MIN 30
#define MCP_BUCKET_CAP 1024
#define MCP_SUB_SLOTS 6
#define MCP_SLOT_SEC 10 /* 6 * 10s = 60s 滑动时间窗口 */

typedef struct {
    bool     active;
    int64_t  user_id; /**< >0 为已认证用户，0 为未认证 IP 请求 */
    char     ip[64];
    int64_t  slot_epoch[MCP_SUB_SLOTS];
    uint32_t slot_count[MCP_SUB_SLOTS];
    int64_t  last_seen;
} mcp_bucket_t;

static mcp_bucket_t    mcp_buckets[MCP_BUCKET_CAP];
static pthread_mutex_t g_mcp_rate_limit_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t
mcp_bucket_sum_and_prune(mcp_bucket_t* b, int64_t now_epoch)
{
    uint32_t sum = 0;
    for (int s = 0; s < MCP_SUB_SLOTS; s++) {
        if (b->slot_count[s] > 0) {
            if ((now_epoch - b->slot_epoch[s]) < MCP_SUB_SLOTS) {
                sum += b->slot_count[s];
            } else {
                b->slot_count[s] = 0;
                b->slot_epoch[s] = 0;
            }
        }
    }
    return sum;
}

static mcp_bucket_t*
mcp_find_or_alloc_bucket(int64_t user_id, const char* safe_ip, int64_t now, uint32_t* out_sum)
{
    int64_t  now_epoch = now / MCP_SLOT_SEC;
    int      empty_idx = -1;
    int      victim_idx = 0;
    uint32_t victim_sum = UINT32_MAX;
    bool     victim_blocked = true;

    for (int i = 0; i < MCP_BUCKET_CAP; i++) {
        mcp_bucket_t* b = &mcp_buckets[i];
        if (!b->active) {
            if (empty_idx < 0) {
                empty_idx = i;
            }
            continue;
        }

        uint32_t sum = mcp_bucket_sum_and_prune(b, now_epoch);
        if (sum == 0 && (now - b->last_seen) > RATE_LIMIT_WINDOW_SEC) {
            b->active = false;
            if (empty_idx < 0) {
                empty_idx = i;
            }
            continue;
        }

        bool match = (user_id > 0)
                         ? (b->user_id == user_id)
                         : (b->user_id == 0 && strncmp(b->ip, safe_ip, sizeof(b->ip) - 1) == 0);
        if (match) {
            *out_sum = sum;
            return b;
        }

        uint32_t b_limit = (b->user_id > 0) ? MCP_RATE_MAX_PER_MIN : MCP_UNAUTH_RATE_MAX_PER_MIN;
        bool     b_blocked = (sum >= b_limit);
        if (i == 0 || !mcp_buckets[victim_idx].active || (!b_blocked && victim_blocked) ||
            (b_blocked == victim_blocked &&
             (sum < victim_sum ||
              (sum == victim_sum && b->last_seen < mcp_buckets[victim_idx].last_seen)))) {
            victim_idx = i;
            victim_sum = sum;
            victim_blocked = b_blocked;
        }
    }

    int           slot = (empty_idx >= 0) ? empty_idx : victim_idx;
    mcp_bucket_t* target = &mcp_buckets[slot];
    memset(target, 0, sizeof(*target));
    target->active = true;
    target->user_id = (user_id > 0) ? user_id : 0;
    if (user_id <= 0) {
        snprintf(target->ip, sizeof(target->ip), "%s", safe_ip);
    }
    target->last_seen = now;
    *out_sum = 0;
    return target;
}

static void
mcp_send_rate_limited(csilk_ctx_t* c)
{
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "jsonrpc", "2.0");
    csilk_json_add_number(resp, "id", 0);
    csilk_json_t* er = csilk_json_object();
    csilk_json_add_number(er, "code", -32005);
    csilk_json_add_string(er, "message", "rate limited: too many MCP requests");
    csilk_json_add_object(resp, "error", er);
    csilk_set_header(c, "Retry-After", "60");
    csilk_json(c, CSILK_STATUS_TOO_MANY_REQUESTS, resp);
    csilk_abort(c);
}

void
mcp_rate_limit_middleware(csilk_ctx_t* c)
{
    const char* path = csilk_get_path(c);
    if (!path || strcmp(path, "/mcp") != 0) {
        csilk_next(c);
        return;
    }

    int64_t     user_id = jwt_get_user_id(c);
    const char* ip = csilk_get_client_ip(c);
    const char* safe_ip = (ip && ip[0]) ? ip : "127.0.0.1";
    int64_t     now = monotonic_now_sec();
    int64_t     now_epoch = now / MCP_SLOT_SEC;
    uint32_t    limit = (user_id > 0) ? MCP_RATE_MAX_PER_MIN : MCP_UNAUTH_RATE_MAX_PER_MIN;

    pthread_mutex_lock(&g_mcp_rate_limit_mutex);
    uint32_t      matches = 0;
    mcp_bucket_t* b = mcp_find_or_alloc_bucket(user_id, safe_ip, now, &matches);
    b->last_seen = now;

    if (matches >= limit) {
        pthread_mutex_unlock(&g_mcp_rate_limit_mutex);
        mcp_send_rate_limited(c);
        return;
    }

    int slot = (int)(now_epoch % MCP_SUB_SLOTS);
    if (b->slot_epoch[slot] != now_epoch) {
        b->slot_epoch[slot] = now_epoch;
        b->slot_count[slot] = 0;
    }
    b->slot_count[slot]++;
    pthread_mutex_unlock(&g_mcp_rate_limit_mutex);

    csilk_next(c);
}
