#pragma once

/**
 * @file rules.h
 * @brief MCP 服务器领域业务规则 (Domain MCP Rules)
 *
 * 纯业务校验函数，零外部依赖。
 * 校验规则对齐设计文档 §3.6 参数白名单。
 */

#include <stdbool.h>

/**
 * @brief 校验 MCP 服务器名称合法性
 * 规则：非空、长度 ≤ 64、仅允许 [a-z0-9_-]
 */
bool mf_mcp_rule_validate_name(const char* name);

/**
 * @brief 校验传输层类型合法性
 * @param transport "http" 或 "stdio"，NULL 视为非法
 */
bool mf_mcp_rule_validate_transport(const char* transport);

/**
 * @brief 校验 http transport 的 URL 合法性
 * 规则：必须 http:// 或 https:// 开头；长度 ≤ 512；
 *       host 不得为内网/回环地址（localhost/127./10./172.16-31./192.168./::1/0.0.0.0），
 *       除非 allow_local=true（对应 MINEFOLIO_MCP_ALLOW_LOCAL=1）。
 */
bool mf_mcp_rule_validate_url(const char* url, bool allow_local);

/**
 * @brief 校验 stdio transport 的命令合法性
 * 规则：长度 ≤ 512，禁止 shell 元字符 ; & | ` $ > < （防 shell 注入；
 *       运行时直接 execvp，不走 shell，此处为纵深防御）
 */
bool mf_mcp_rule_validate_command(const char* command);

/**
 * @brief 校验超时参数
 * 规则：1000 ≤ timeout_ms ≤ 120000
 */
bool mf_mcp_rule_validate_timeout_ms(int timeout_ms);
