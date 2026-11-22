/**
 * @file rate_limit.c
 * @brief 认证相关敏感接口及 /mcp 端点限流中间件实现
 *
 * 实现了基于单调时钟 (CLOCK_MONOTONIC) 与滑动时间窗口算法的 IP/用户限流，
 * 并使用 POSIX 互斥锁确保多线程并发请求时的计数安全。
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
#define RATE_LIMIT_RING 4096

typedef struct {
    int64_t ts_mono;  /**< 单调时钟时间戳（秒） */
    char    path[64]; /**< 归一化目标路径 */
    char    ip[64];   /**< 客户端 IP 地址 */
} entry_t;

static entry_t         ring[RATE_LIMIT_RING];
static int             ring_head = 0;
static int             ring_count = 0;
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
evict_old(int64_t now)
{
    while (ring_count > 0 && (now - ring[ring_head].ts_mono) > RATE_LIMIT_WINDOW_SEC) {
        ring_head = (ring_head + 1) % RATE_LIMIT_RING;
        ring_count--;
    }
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
    evict_old(now);

    int matches = 0;
    for (int i = 0; i < ring_count; i++) {
        int idx = (ring_head + i) % RATE_LIMIT_RING;
        if (ring[idx].ts_mono <= 0) {
            continue;
        }
        if ((now - ring[idx].ts_mono) <= RATE_LIMIT_WINDOW_SEC &&
            strncmp(ring[idx].path, limit_path, sizeof(ring[idx].path) - 1) == 0 &&
            strncmp(ring[idx].ip, safe_ip, sizeof(ring[idx].ip) - 1) == 0) {
            matches++;
        }
    }

    if (matches >= RATE_LIMIT_MAX_REQS) {
        pthread_mutex_unlock(&g_rate_limit_mutex);
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_number(resp, "code", 1004);
        csilk_json_add_string(resp, "message", "请求过于频繁，请稍后再试");
        csilk_set_header(c, "Retry-After", "60");
        csilk_json(c, CSILK_STATUS_TOO_MANY_REQUESTS, resp);
        csilk_abort(c);
        return;
    }

    if (ring_count >= RATE_LIMIT_RING) {
        ring_head = (ring_head + 1) % RATE_LIMIT_RING;
        ring_count--;
    }
    int idx = (ring_head + ring_count) % RATE_LIMIT_RING;
    ring[idx].ts_mono = now;
    snprintf(ring[idx].path, sizeof(ring[idx].path), "%s", limit_path);
    snprintf(ring[idx].ip, sizeof(ring[idx].ip), "%s", safe_ip);
    ring_count++;

    pthread_mutex_unlock(&g_rate_limit_mutex);

    csilk_next(c);
}

/* ================================================================== */
/* /mcp 每用户 & 未认证 IP 限流（防认证/未认证洪水请求）               */
/* ================================================================== */

#define MCP_RATE_MAX_PER_MIN 120
#define MCP_UNAUTH_RATE_MAX_PER_MIN 30
#define MCP_RATE_RING 1024

typedef struct {
    int64_t user_id; /**< >0 为已认证用户，0 为未认证 IP 请求 */
    int64_t ts_mono; /**< 单调时钟时间戳（秒） */
    char    ip[64];  /**< 未认证请求使用的客户端 IP */
} mcp_entry_t;

static mcp_entry_t     mcp_ring[MCP_RATE_RING];
static int             mcp_ring_head = 0;
static int             mcp_ring_count = 0;
static pthread_mutex_t g_mcp_rate_limit_mutex = PTHREAD_MUTEX_INITIALIZER;

static void
mcp_evict_old(int64_t now)
{
    while (mcp_ring_count > 0 && (now - mcp_ring[mcp_ring_head].ts_mono) > RATE_LIMIT_WINDOW_SEC) {
        mcp_ring_head = (mcp_ring_head + 1) % MCP_RATE_RING;
        mcp_ring_count--;
    }
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

    pthread_mutex_lock(&g_mcp_rate_limit_mutex);
    mcp_evict_old(now);

    int matches = 0;
    int limit = (user_id > 0) ? MCP_RATE_MAX_PER_MIN : MCP_UNAUTH_RATE_MAX_PER_MIN;
    for (int i = 0; i < mcp_ring_count; i++) {
        int idx = (mcp_ring_head + i) % MCP_RATE_RING;
        if (mcp_ring[idx].ts_mono <= 0 || (now - mcp_ring[idx].ts_mono) > RATE_LIMIT_WINDOW_SEC) {
            continue;
        }
        if (user_id > 0) {
            if (mcp_ring[idx].user_id == user_id) {
                matches++;
            }
        } else {
            if (mcp_ring[idx].user_id == 0 &&
                strncmp(mcp_ring[idx].ip, safe_ip, sizeof(mcp_ring[idx].ip) - 1) == 0) {
                matches++;
            }
        }
    }

    if (matches >= limit) {
        pthread_mutex_unlock(&g_mcp_rate_limit_mutex);
        mcp_send_rate_limited(c);
        return;
    }

    if (mcp_ring_count >= MCP_RATE_RING) {
        mcp_ring_head = (mcp_ring_head + 1) % MCP_RATE_RING;
        mcp_ring_count--;
    }
    int idx = (mcp_ring_head + mcp_ring_count) % MCP_RATE_RING;
    mcp_ring[idx].user_id = (user_id > 0) ? user_id : 0;
    mcp_ring[idx].ts_mono = now;
    if (user_id <= 0) {
        snprintf(mcp_ring[idx].ip, sizeof(mcp_ring[idx].ip), "%s", safe_ip);
    } else {
        mcp_ring[idx].ip[0] = '\0';
    }
    mcp_ring_count++;

    pthread_mutex_unlock(&g_mcp_rate_limit_mutex);

    csilk_next(c);
}
