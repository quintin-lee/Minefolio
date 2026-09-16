#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file mcp_config.h
 * @brief MCP 运行时环境旋钮（MINEFOLIO_MCP_* 系列），统一经 config/secret.h 读取
 *
 * 设计 docs/design/MCP_SUPPORT_DESIGN.md §9。每个函数返回带默认的整型值，
 * 未配置或非法（<0）时返回 0 表示「禁用该限制」。
 */

/** MCP 工具缓存 TTL（秒）。0 表示不启用 TTL 过期。 */
int mf_mcp_config_cache_ttl_sec(void);

/** stdio 子进程池上限。0 表示禁用池（每次调用重新 spawn）。 */
int mf_mcp_config_stdio_max_procs(void);

/** 每用户每分钟 MCP 调用限频。0 表示不启用 MCP 专属限频。 */
int mf_mcp_config_rate_per_min(void);

/** 是否允许内网/回环 MCP 服务器地址（MINEFOLIO_MCP_ALLOW_LOCAL=1）。 */
bool mf_mcp_config_allow_local(void);

#ifdef __cplusplus
}
#endif
