#pragma once

/**
 * @file ai_session_repo_impl.h
 * @brief 基础设施层 AI 会话仓储实现头文件
 */

#include "csilk/csilk.h"
#include <stdint.h>

csilk_json_t* mf_ai_session_list(
    csilk_db_pool_t* pool, int64_t user_id, int64_t page, int64_t page_size, int64_t* total);

csilk_json_t* mf_ai_session_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

int64_t mf_ai_session_insert(csilk_db_pool_t* pool,
                             int64_t          user_id,
                             const char*      title,
                             const char*      model,
                             const char*      provider);

int mf_ai_session_update(
    csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* title, const char* model);

int mf_ai_session_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

csilk_json_t* mf_ai_message_list(
    csilk_db_pool_t* pool, int64_t session_id, int64_t page, int64_t page_size, int64_t* total);

csilk_json_t* mf_ai_message_recent(csilk_db_pool_t* pool, int64_t session_id, int limit);

int64_t mf_ai_message_insert(csilk_db_pool_t* pool,
                             int64_t          session_id,
                             const char*      role,
                             const char*      content,
                             const char*      model,
                             const char*      thinking_content,
                             const char*      tool_calls_json);

int mf_ai_message_delete_last_assistant(csilk_db_pool_t* pool, int64_t session_id);

/**
 * @brief 删除会话中 id >= from_id 的全部消息（编辑重发前的历史截断）
 * @param pool 数据库连接池
 * @param session_id 会话 ID
 * @param from_id 起始消息 ID（包含）；传一个不存在的大 ID 时等价于不删除
 * @return 实际删除的行数；失败返回 -1
 */
int mf_ai_messages_delete_from(csilk_db_pool_t* pool, int64_t session_id, int64_t from_id);

char* mf_ai_summary_get(csilk_db_pool_t* pool, int64_t session_id);

int mf_ai_summary_upsert(csilk_db_pool_t* pool,
                         int64_t          session_id,
                         int64_t          user_id,
                         const char*      summary_text,
                         int              token_count);
