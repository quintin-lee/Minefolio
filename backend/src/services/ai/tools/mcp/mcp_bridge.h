#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "csilk/protocols/mcp.h"
#include "domain/mcp/entity.h"
#include "domain/mcp/repository.h"
#include "services/ai/tools/context.h"

/**
 * @file mcp_bridge.h
 * @brief MCP 远端工具桥接层 — M2
 *
 * 将用户已配置并探活过的 MCP 服务器上的远端工具，桥接为 LLM 可消费的
 * csilk_ai_tool_t 定义，并在运行时按 "mcp:<serverId>:<tool>" 前缀把调用
 * 路由到远端 MCP 服务器执行。
 *
 * 三条关键入口：
 *  - mcp_bridge_merge_tools()：内置工具 + 当前用户已启用 MCP 服务器缓存工具
 *    合并为一个堆分配 csilk_ai_tool_t* 数组（loop.c step4 喂给 LLM，step10 释放）
 *  - mcp_bridge_dispatch()：dispatcher.c 对 "mcp:" 前缀工具名的路由目标，
 *    完整 8 步链路（认证→寻址→schema→R-MCP 策略→执行→审计→trace）
 *  - mcp_bridge_refresh_server()：controller /test + /refresh 实装，
 *    initialize + tools/list 回写 mcp_server_tool 缓存 + discovered_at
 */

/**
 * @brief 合并当前用户的内置 + MCP 工具定义。
 *
 * @param pool      DB pool（取 MCP 工具缓存）
 * @param user_id   用户 ID
 * @param out_count [out] 合并后工具总数
 * @return 堆分配的 csilk_ai_tool_t* 数组（调用方 free），或 NULL
 *
 * 内置工具 name 为静态串，MCP 工具 name 为 "mcp:<id>:<tool>" 堆串，
 * description/parameters_json 也随条目堆分配；调用方必须逐条 free 指针后
 * 再 free 数组本身（见 mcp_bridge_free_merged）。
 */
const csilk_ai_tool_t*
mcp_bridge_merge_tools(csilk_db_pool_t* pool, int64_t user_id, size_t* out_count);

/**
 * @brief 释放 mcp_bridge_merge_tools 返回的数组及其内部堆指针。
 *
 * 仅能安全释放 merge_tools 返回的数组（内置工具数组是静态的，不应传入）。
 */
void mcp_bridge_free_merged(const csilk_ai_tool_t* arr, size_t count);

/**
 * @brief 路由 "mcp:<serverId>:<tool>" 工具调用到远端 MCP 服务器。
 *
 * 由 dispatcher.c 在 ai_tool_dispatch_parsed 入口检测到 "mcp:" 前缀后调用。
 * 返回堆分配 JSON 结果字符串（调用方 free），或 NULL。
 *
 * @param ctx      工具上下文（pool/user_id/session_id/trace_id/permissions）
 * @param tool_name 形如 "mcp:7:query_orders"
 * @param args     工具参数 JSON 对象
 */
char*
mcp_bridge_dispatch(const ai_tool_context_t* ctx, const char* tool_name, const csilk_json_t* args);

/**
 * @brief 探活并刷新某 MCP 服务器：initialize + tools/list，回写缓存与 discovered_at。
 *
 * @param pool       DB pool
 * @param user_id    用户 ID（做权限校验）
 * @param server_id  目标服务器
 * @param out_count  [out] 刷新后该服务器缓存的工具数
 * @param out_err    [out] 失败原因（调用方释放）
 * @return 0 成功, -1 失败
 */
int mcp_bridge_refresh_server(
    csilk_db_pool_t* pool, int64_t user_id, int64_t server_id, size_t* out_count, char** out_err);
