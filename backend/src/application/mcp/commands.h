#pragma once

/**
 * @file commands.h
 * @brief MCP 服务器用例命令对象 (MCP Application Commands)
 */

#include <stdbool.h>
#include <stdint.h>

/** 创建 MCP 服务器命令 */
typedef struct {
    int64_t     user_id;
    const char* name;
    const char* transport; /* "http" | "stdio" */
    const char* url;
    const char* command;
    const char* args_json;
    const char* env_json;
    const char* headers_json;
    const char* secret_ref;
    int         timeout_ms;
} mf_mcp_create_cmd_t;

/** 更新 MCP 服务器命令 */
typedef struct {
    int64_t     user_id;
    int64_t     server_id;
    const char* name;
    const char* transport;
    const char* url;
    const char* command;
    const char* args_json;
    const char* env_json;
    const char* headers_json;
    const char* secret_ref;
    int         timeout_ms;
    bool        enabled;
} mf_mcp_update_cmd_t;

/** 删除 MCP 服务器命令 */
typedef struct {
    int64_t user_id;
    int64_t server_id;
} mf_mcp_delete_cmd_t;
