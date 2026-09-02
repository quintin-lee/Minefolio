#pragma once
#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @file ai_session_repo.h
 * @brief AI 会话与消息数据访问层接口定义
 *
 * 提供 AI 对话会话 (ai_sessions) 及其关联消息记录 (ai_messages) 的 CRUD 数据持久化操作。
 * 包含会话列表分页、增删改查、上下文消息检索及最后助手消息回滚等数据库交互方法。
 */

/**
 * @brief 分页查询指定用户的 AI 会话列表
 *
 * 执行两阶段查询：
 * 1. 统计满足 `user_id` 条件的总会话数并写入 `total`。
 * 2. 按 `updated_at DESC` 降序分页获取会话列表。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param page 当前页码 (从 1 开始)
 * @param page_size 每页数据条数
 * @param[out] total 输出参数，返回符合条件的总记录数指针 (非空)
 * @return csilk_json_t* 包含会话对象的 JSON 数组，每个对象包含 id, user_id, title, model, provider, created_at, updated_at；出错或无数据时返回空数组或 NULL
 */
csilk_json_t* ai_session_list(
    csilk_db_pool_t* pool, int64_t user_id, int64_t page, int64_t page_size, int64_t* total);

/**
 * @brief 根据会话 ID 查询指定用户的单个 AI 会话详情
 *
 * 执行参数化查询 `WHERE id=? AND user_id=?` 防止越权访问。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 会话 ID
 * @return csilk_json_t* 包含单个会话对象的 JSON 数组（长度为 1）；若未找到或查询失败返回 NULL
 */
csilk_json_t* ai_session_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 创建新的 AI 会话记录
 *
 * 插入会话记录并返回新生成的自增主键 ID。若未指定模型或提供商，将使用系统预设默认值。
 *
 * @param pool 数据库连接池指针
 * @param user_id 关联的用户 ID
 * @param title 会话标题（为 NULL 时默认为 "新对话"）
 * @param model 使用的 AI 模型名称（为 NULL 时默认为 "deepseek-chat"）
 * @param provider AI 服务提供商名称（为 NULL 时默认为 "deepseek"）
 * @return int64_t 成功时返回新创建的会话 ID，失败返回 0
 */
int64_t ai_session_insert(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          const char*      title,
                          const char*      model,
                          const char*      provider);

/**
 * @brief 更新指定 AI 会话的标题和/或模型配置
 *
 * 动态拼接 SQL 更新字段，并自动刷新 `updated_at=CURRENT_TIMESTAMP`。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（用于鉴权隔离）
 * @param id 会话 ID
 * @param title 新会话标题（为 NULL 或空字符串时不更新该字段）
 * @param model 新模型名称（为 NULL 或空字符串时不更新该字段）
 * @return int 成功更新返回 1，未修改或失败返回 0
 */
int ai_session_update(
    csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* title, const char* model);

/**
 * @brief 删除指定的 AI 会话
 *
 * 根据外键约束级联删除或由上层业务控制关联消息的生命周期。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（防止跨租户越权删除）
 * @param id 会话 ID
 * @return int 删除成功（影响行数 > 0）返回 1，否则返回 0
 */
int ai_session_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 分页查询指定会话的历史消息记录
 *
 * 按消息创建时间正序 (`created_at ASC`) 返回消息记录，适合聊天流滚动展示。
 *
 * @param pool 数据库连接池指针
 * @param session_id 会话 ID
 * @param page 当前页码 (从 1 开始)
 * @param page_size 每页消息条数
 * @param[out] total 输出参数，返回该会话下的总消息数
 * @return csilk_json_t* 包含消息对象的 JSON 数组，字段包含 id, session_id, role, content, model, created_at
 */
csilk_json_t* ai_message_list(
    csilk_db_pool_t* pool, int64_t session_id, int64_t page, int64_t page_size, int64_t* total);

/**
 * @brief 获取指定会话最近的 N 条消息上下文
 *
 * 使用子查询先取按时间逆序最近的 `limit` 条记录，再在外层按时间正序重新排列，
 * 保证输入给大语言模型 (LLM) 上下文的时序正确性。
 *
 * @param pool 数据库连接池指针
 * @param session_id 会话 ID
 * @param limit 获取的最大消息条数
 * @return csilk_json_t* 包含 role 和 content 字段的 JSON 数组（按时间正序排列）
 */
csilk_json_t* ai_message_recent(csilk_db_pool_t* pool, int64_t session_id, int limit);

/**
 * @brief 向指定会话追加一条新的消息记录
 *
 * 记录对话角色 (user/assistant/system/tool)、消息文本及所使用的模型名称。
 *
 * @param pool 数据库连接池指针
 * @param session_id 关联的会话 ID
 * @param role 角色标识符 (如 "user", "assistant", "system")
 * @param content 消息文本内容
 * @param model 生成该消息使用的模型标识（可选）
 * @return int64_t 成功时返回新消息的主键 ID，失败返回 0
 */
int64_t ai_message_insert(csilk_db_pool_t* pool,
                          int64_t          session_id,
                          const char*      role,
                          const char*      content,
                          const char*      model);

/**
 * @brief 删除指定会话中最后一条助手的回复消息
 *
 * 用于 AI 重新生成回答 (Regenerate) 场景，通过子查询找到该会话中 ID 最大的 assistant 消息并执行删除。
 *
 * @param pool 数据库连接池指针
 * @param session_id 会话 ID
 * @return int 成功删除的记录行数（通常为 1 或 0）
 */
int ai_message_delete_last_assistant(csilk_db_pool_t* pool, int64_t session_id);
