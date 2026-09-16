#pragma once

#include <stddef.h>

#include "domain/mcp/entity.h"
#include "services/ai/tools/mcp/mcp_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file mcp_stdio_pool.h
 * @brief stdio MCP 客户端会话池 — 跨 dispatch 复用已 initialize 的子进程
 *
 * 设计 docs/design/MCP_SUPPORT_DESIGN.md §10：stdio 子进程 spawn 开销大（fork+execve
 * + MCP 握手），池化后同 (user, server) 在 TTL 窗口内复用同一客户端，避免每次
 * dispatch 重新 spawn。池上限 MINEFOLIO_MCP_STDIO_MAX_PROCS，空闲超过
 * MINEFOLIO_MCP_CACHE_TTL 的条目在下次 acquire/refresh 时 SIGTERM→SIGKILL 双段回收。
 *
 * 线程安全：内部 pthread_mutex 保护，可被多线程（csilk event loop worker）并发 acquire。
 */

/**
 * 获取（user, server）对应的已初始化 stdio 客户端；不存在则 new+initialize 入池。
 * 返回 NULL 且 *err 非空表示失败（OOM/spawn/握手失败）。
 * 调用方用完后 MUST 调 mf_mcp_stdio_pool_release 归还（不关闭子进程，仅标记空闲）。
 * 池上限满时自动 LRU 淘汰最久未用条目。
 */
mf_mcp_client_t* mf_mcp_stdio_pool_acquire(
    int64_t user_id, int64_t server_id, const mf_mcp_server_t* server, char* err, size_t err_sz);

/** 归还池占位（不关闭子进程）。client 若非池条目则直接 mf_mcp_client_free。 */
void mf_mcp_stdio_pool_release(mf_mcp_client_t* client);

/** 主动回收空闲超过 TTL 的池条目（SIGTERM→SIGKILL）。返回回收数量。 */
int mf_mcp_stdio_pool_evict_idle(void);

/** 强制销毁整个池（进程退出/测试清理用）。返回销毁条目数。 */
int mf_mcp_stdio_pool_shutdown(void);

#ifdef __cplusplus
}
#endif
