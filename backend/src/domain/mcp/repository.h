#pragma once

/**
 * @file repository.h
 * @brief MCP 服务器仓储抽象契约接口 (Domain MCP Repository Contract)
 *
 * 纯 C 契约，入参出参仅允许领域实体与标量数值，严禁返回 JSON 节点或编写 SQL。
 * 由 infrastructure/repositories/mcp_server_repo_impl.c 实现。
 *
 * 返回值约定（与 tag 仓储一致）：
 *   - list/加载类：0 成功, -1 数据库错误
 *   - find 单条：0 成功查到, 1 不存在, -1 数据库错误
 *   - create：0 成功, -1 失败
 *   - update/delete：0 成功, 1 不存在, -1 数据库错误
 */

#include <stddef.h>
#include <stdint.h>
#include "domain/mcp/entity.h"

/**
 * @brief 查询指定用户的所有 MCP 服务器
 */
int mf_mcp_server_repo_list(void*             db_pool,
                            int64_t           user_id,
                            mf_mcp_server_t** out_list,
                            size_t*           out_count);

/**
 * @brief 根据 ID 查询单个 MCP 服务器
 * @return 0 成功查到, 1 不存在, -1 数据库错误
 */
int mf_mcp_server_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_mcp_server_t* out);

/**
 * @brief 持久化保存新 MCP 服务器
 * @return 0 成功, -1 失败
 */
int mf_mcp_server_repo_create(void*                  db_pool,
                              int64_t                user_id,
                              const mf_mcp_server_t* s,
                              int64_t*               out_id);

/**
 * @brief 更新 MCP 服务器（全量更新可变字段）
 * @return 0 成功, 1 不存在, -1 数据库错误
 */
int mf_mcp_server_repo_update(void* db_pool, int64_t user_id, int64_t id, const mf_mcp_server_t* s);

/**
 * @brief 删除 MCP 服务器（级联删除其 mcp_server_tool 缓存行）
 * @return 0 成功, 1 不存在, -1 数据库错误
 */
int mf_mcp_server_repo_delete(void* db_pool, int64_t user_id, int64_t id);

/**
 * @brief 释放由仓储分配的实体数组内存
 */
void mf_mcp_server_repo_free_list(mf_mcp_server_t* list, size_t count);

/* ---- mcp_server_tool 缓存操作 ---- */

/**
 * @brief 加载某服务器已缓存的工具 schema
 * @return 0 成功, -1 数据库错误
 */
int mf_mcp_server_tool_repo_load(void*                  db_pool,
                                 int64_t                user_id,
                                 int64_t                server_id,
                                 mf_mcp_server_tool_t** out_list,
                                 size_t*                out_count);

/**
 * @brief 批量 upsert 工具 schema 缓存（先删该 server 旧缓存再插入）
 * @return 0 成功, -1 数据库错误
 */
int mf_mcp_server_tool_repo_upsert(void*                       db_pool,
                                   int64_t                     user_id,
                                   int64_t                     server_id,
                                   const mf_mcp_server_tool_t* tools,
                                   size_t                      count);

/**
 * @brief 加载某用户名下所有已启用服务器 + 工具（供 bridge 合并 LLM 工具集用）
 * 简化：仅返回工具列表（qualified_name 已前缀化）。
 * @return 0 成功, -1 数据库错误
 */
int mf_mcp_server_tool_repo_load_for_user(void*                  db_pool,
                                          int64_t                user_id,
                                          mf_mcp_server_tool_t** out_list,
                                          size_t*                out_count);

/**
 * @brief 释放工具实体数组内存
 */
void mf_mcp_server_tool_repo_free_list(mf_mcp_server_tool_t* list, size_t count);
