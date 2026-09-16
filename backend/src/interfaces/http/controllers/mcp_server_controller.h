#pragma once

#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file mcp_server_controller.h
 * @brief Path B — 将 Minefolio 暴露为 MCP 服务器（streamable-HTTP JSON-RPC 2.0）
 *
 * 在 POST /mcp 上实现 MCP 协议的 initialize / tools/list / tools/call，
 * 复用内置 ai_tool_t 注册表作为 tools/call 的执行体。
 * 与现有 REST controller 不同，本端点返回原始 JSON-RPC 报文（不套 {code,message,data} 信封）。
 */

void register_mcp_server_routes(csilk_app_t* app);

#ifdef __cplusplus
}
#endif
