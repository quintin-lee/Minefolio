#pragma once

/**
 * @file entity.h
 * @brief MCP 服务器领域实体定义 (Domain MCP Entity)
 *
 * 纯 C 结构体，零外部依赖，仅使用标准库类型。
 * 对应数据库表 mcp_server / mcp_server_tool。
 */

#include <stdbool.h>
#include <stdint.h>

/** MCP 传输层类型 */
typedef enum {
    MCP_TRANSPORT_HTTP = 0,
    MCP_TRANSPORT_STDIO = 1,
} mf_mcp_transport_t;

/** MCP 服务器聚合根实体 */
typedef struct {
    int64_t            id;
    int64_t            user_id;
    char               name[128];
    mf_mcp_transport_t transport;
    char               url[512];
    char               command[512];
    char               args_json[1024];
    char               env_json[2048];
    char               headers_json[2048];
    char               secret_ref[128];
    bool               enabled;
    int                timeout_ms;
    char               discovered_at[32];
    char               created_at[32];
    char               updated_at[32];
} mf_mcp_server_t;

#define MF_MCP_TOOL_SCHEMA_MAX 16384

/** MCP 工具 schema 缓存实体 */
typedef struct {
    int64_t id;
    int64_t server_id;
    int64_t user_id;
    char    tool_name[128];
    char    qualified_name[192];
    char    description[512];
    char    input_schema[MF_MCP_TOOL_SCHEMA_MAX];
    bool    is_mutation;
    char    risk_level[16];
    char    fetched_at[32];
} mf_mcp_server_tool_t;
