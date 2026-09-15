#pragma once

/**
 * @file usecases.h
 * @brief MCP 服务器用例接口声明 (MCP Application Use Cases)
 */

#include "csilk/csilk.h"
#include "application/mcp/commands.h"
#include "application/mcp/dtos.h"

/**
 * @brief 查询用户所有 MCP 服务器（含每个服务器的工具缓存数量）
 */
int mf_mcp_usecase_list(void* pool, int64_t user_id, csilk_json_t** out_list);

/**
 * @brief 查询单个 MCP 服务器
 * @return 0 成功, -1 查询错误
 */
int mf_mcp_usecase_get(void* pool, int64_t user_id, int64_t server_id, csilk_json_t** out_obj);

/**
 * @brief 创建 MCP 服务器
 */
int mf_mcp_usecase_create(void*                      pool,
                          const mf_mcp_create_cmd_t* cmd,
                          int64_t*                   out_id,
                          mf_mcp_usecase_result_t*   out_res);

/**
 * @brief 更新 MCP 服务器
 */
int
mf_mcp_usecase_update(void* pool, const mf_mcp_update_cmd_t* cmd, mf_mcp_usecase_result_t* out_res);

/**
 * @brief 删除 MCP 服务器
 */
int
mf_mcp_usecase_delete(void* pool, const mf_mcp_delete_cmd_t* cmd, mf_mcp_usecase_result_t* out_res);

/**
 * @brief 读取某服务器已缓存的工具列表（不重拉）
 */
int mf_mcp_usecase_tools(void* pool, int64_t user_id, int64_t server_id, csilk_json_t** out_list);
