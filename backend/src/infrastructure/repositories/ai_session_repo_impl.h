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
                             const char*      model);

int mf_ai_message_delete_last_assistant(csilk_db_pool_t* pool, int64_t session_id);
