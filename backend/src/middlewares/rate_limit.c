/**
 * @file rate_limit.c
 * @brief 认证相关敏感接口请求频率限制（限流）中间件实现
 *
 * 实现了基于内存环形队列（Ring Buffer）与滑动时间窗口算法的 IP 级别接口限流，
 * 并使用 POSIX 互斥锁确保多线程并发请求时的计数安全。
 */

#include "middlewares/rate_limit.h"
#include "common/response.h"
#include "common/ctx.h"
#include "csilk/core/response.h"
#include <pthread.h>
#include <string.h>
#include <time.h>

/**
 * @def RATE_LIMIT_WINDOW_SEC
 * @brief 滑动窗口时间跨度（秒）
 */
#define RATE_LIMIT_WINDOW_SEC 60

/**
 * @def RATE_LIMIT_MAX_REQS
 * @brief 窗口时间内单个 IP 对单个接口的最大允许请求次数
 */
#define RATE_LIMIT_MAX_REQS 10

/**
 * @def RATE_LIMIT_RING
 * @brief 环形缓冲区最大容量（记录数）
 */
#define RATE_LIMIT_RING 64

/**
 * @struct entry_t
 * @brief 限流请求历史记录项
 */
typedef struct {
    time_t ts;       /**< 请求发生的时间戳（秒） */
    char   path[64]; /**< 请求的目标路径 */
    char   ip[64];   /**< 客户端 IP 地址 */
} entry_t;

/**
 * @brief 内部静态环形缓冲区数组
 */
static entry_t ring[RATE_LIMIT_RING];

/**
 * @brief 内部静态变量：环形缓冲区队头索引
 */
static int ring_head = 0;

/**
 * @brief 内部静态变量：环形缓冲区当前有效记录数
 */
static int ring_count = 0;

/**
 * @brief 内部静态变量：保护环形缓冲区并发读写的互斥锁
 */
static pthread_mutex_t g_rate_limit_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief 淘汰环形缓冲区中超出时间窗口的过期记录
 *
 * @param[in] now 当前系统时间戳
 *
 * @note 必须在持有 g_rate_limit_mutex 锁的前提下调用。
 */
static void
evict_old(time_t now)
{
    while (ring_count > 0 && (now - ring[ring_head].ts) > RATE_LIMIT_WINDOW_SEC) {
        ring_head = (ring_head + 1) % RATE_LIMIT_RING;
        ring_count--;
    }
}

/**
 * @brief 认证敏感接口限流中间件处理函数
 *
 * @param[in,out] c HTTP 请求上下文
 */
void
rate_limit_auth_middleware(csilk_ctx_t* c)
{
    const char* path = csilk_get_path(c);
    if (!path) {
        csilk_next(c);
        return;
    }

    /* 仅对认证与安全设置类写接口执行限流 */
    if (strcmp(path, "/api/auth/login") != 0 && strcmp(path, "/api/auth/register") != 0 &&
        strcmp(path, "/api/system/setup") != 0 && strcmp(path, "/api/auth/2fa/verify-login") != 0) {
        csilk_next(c);
        return;
    }

    const char* ip = csilk_get_client_ip(c);
    const char* safe_ip = (ip && ip[0]) ? ip : "127.0.0.1";
    time_t      now = time(NULL);

    pthread_mutex_lock(&g_rate_limit_mutex);
    evict_old(now);

    int matches = 0;
    for (int i = 0; i < ring_count; i++) {
        int idx = (ring_head + i) % RATE_LIMIT_RING;
        if (ring[idx].ts == 0) {
            break;
        }
        if ((now - ring[idx].ts) <= RATE_LIMIT_WINDOW_SEC &&
            strncmp(ring[idx].path, path, sizeof(ring[idx].path) - 1) == 0 &&
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

    /* 记录当前请求到环形缓冲区 */
    if (ring_count >= RATE_LIMIT_RING) {
        ring_head = (ring_head + 1) % RATE_LIMIT_RING;
        ring_count--;
    }
    int idx = (ring_head + ring_count) % RATE_LIMIT_RING;
    ring[idx].ts = now;
    strncpy(ring[idx].path, path, sizeof(ring[idx].path) - 1);
    ring[idx].path[sizeof(ring[idx].path) - 1] = '\0';
    strncpy(ring[idx].ip, safe_ip, sizeof(ring[idx].ip) - 1);
    ring[idx].ip[sizeof(ring[idx].ip) - 1] = '\0';
    ring_count++;

    pthread_mutex_unlock(&g_rate_limit_mutex);

    csilk_next(c);
}

/* ================================================================== */
/* /mcp 每用户限流（MINEFOLIO_MCP 端点防 hammering）                  */
/* 与认证限流完全隔离：独立环形缓冲 + 独立互斥锁 + 按 user_id 维度。   */
/* ================================================================== */

/**
 * @def MCP_RATE_MAX_PER_MIN
 * @brief MCP 端点每用户每分钟最大允许请求数
 */
#define MCP_RATE_MAX_PER_MIN 120

/**
 * @def MCP_RATE_RING
 * @brief MCP 环形缓冲区最大容量
 */
#define MCP_RATE_RING 256

/**
 * @struct mcp_entry_t
 * @brief MCP 限流历史记录项（按 user_id 维度，区别于认证限流的 IP+path）
 */
typedef struct {
    int64_t user_id; /**< 请求的用户 ID（来自 JWT） */
    time_t  ts;      /**< 请求发生的时间戳（秒） */
} mcp_entry_t;

/**
 * @brief MCP 独立环形缓冲区数组
 */
static mcp_entry_t mcp_ring[MCP_RATE_RING];

/**
 * @brief MCP 环形缓冲区队头索引
 */
static int mcp_ring_head = 0;

/**
 * @brief MCP 环形缓冲区当前有效记录数
 */
static int mcp_ring_count = 0;

/**
 * @brief 保护 MCP 环形缓冲区并发读写的互斥锁
 */
static pthread_mutex_t g_mcp_rate_limit_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief 淘汰 MCP 环形缓冲区中超出时间窗口的过期记录
 *
 * @param[in] now 当前系统时间戳
 *
 * @note 必须在持有 g_mcp_rate_limit_mutex 锁的前提下调用。
 */
static void
mcp_evict_old(time_t now)
{
    while (mcp_ring_count > 0 && (now - mcp_ring[mcp_ring_head].ts) > RATE_LIMIT_WINDOW_SEC) {
        mcp_ring_head = (mcp_ring_head + 1) % MCP_RATE_RING;
        mcp_ring_count--;
    }
}

/**
 * @brief MCP 端点每用户限流中间件处理函数
 *
 * 仅拦截 /mcp 路径（streamable-HTTP JSON-RPC 端点），按 user_id 维度滑动窗口限频。
 * 中间件须注册在 JWT 组之后，使 ctx_user_id(c) 可用。
 */
void
mcp_rate_limit_middleware(csilk_ctx_t* c)
{
    const char* path = csilk_get_path(c);
    if (!path || strcmp(path, "/mcp") != 0) {
        csilk_next(c);
        return;
    }

    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        /* 未认证：交由 JWT 中间件拦截（/mcp 组已挂 JWT），此处直接放行 */
        csilk_next(c);
        return;
    }

    time_t now = time(NULL);
    pthread_mutex_lock(&g_mcp_rate_limit_mutex);
    mcp_evict_old(now);

    int matches = 0;
    for (int i = 0; i < mcp_ring_count; i++) {
        int idx = (mcp_ring_head + i) % MCP_RATE_RING;
        if (mcp_ring[idx].user_id == 0) {
            break;
        }
        if ((now - mcp_ring[idx].ts) <= RATE_LIMIT_WINDOW_SEC && mcp_ring[idx].user_id == user_id) {
            matches++;
        }
    }

    if (matches >= MCP_RATE_MAX_PER_MIN) {
        pthread_mutex_unlock(&g_mcp_rate_limit_mutex);
        /* /mcp 为裸 JSON-RPC 端点（不套 {code,message,data} 信封），
           超限须回 JSON-RPC 错误帧而非信封。 */
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
        return;
    }

    /* 记录当前请求到 MCP 环形缓冲区 */
    if (mcp_ring_count >= MCP_RATE_RING) {
        mcp_ring_head = (mcp_ring_head + 1) % MCP_RATE_RING;
        mcp_ring_count--;
    }
    int idx = (mcp_ring_head + mcp_ring_count) % MCP_RATE_RING;
    mcp_ring[idx].user_id = user_id;
    mcp_ring[idx].ts = now;
    mcp_ring_count++;

    pthread_mutex_unlock(&g_mcp_rate_limit_mutex);

    csilk_next(c);
}
