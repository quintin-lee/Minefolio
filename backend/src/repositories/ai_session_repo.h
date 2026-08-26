#pragma once
#include "csilk/csilk.h"
#include <stdint.h>

csilk_json_t* ai_session_list(
    csilk_db_pool_t* pool, int64_t user_id, int64_t page, int64_t page_size, int64_t* total);
csilk_json_t* ai_session_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int64_t       ai_session_insert(csilk_db_pool_t* pool,
                                int64_t          user_id,
                                const char*      title,
                                const char*      model,
                                const char*      provider);
int           ai_session_update(
    csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* title, const char* model);
int ai_session_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

csilk_json_t* ai_message_list(
    csilk_db_pool_t* pool, int64_t session_id, int64_t page, int64_t page_size, int64_t* total);
csilk_json_t* ai_message_recent(csilk_db_pool_t* pool, int64_t session_id, int limit);
int64_t       ai_message_insert(csilk_db_pool_t* pool,
                                int64_t          session_id,
                                const char*      role,
                                const char*      content,
                                const char*      model);
int           ai_message_delete_last_assistant(csilk_db_pool_t* pool, int64_t session_id);
