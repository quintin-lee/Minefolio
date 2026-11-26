#pragma once

/**
 * @file im_bot_usecases.h
 * @brief IM 机器人只读查账与绑定用例接口
 */

#include "csilk/csilk.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

char* im_bot_usecase_generate_bind_token(void* pool, csilk_ctx_t* c, int64_t user_id);

csilk_json_t* im_bot_usecase_list_bindings(void* pool, int64_t user_id);

int
im_bot_usecase_unbind(void* pool, const char* platform, const char* im_user_id, int64_t user_id);

int im_bot_usecase_bind(void*          pool,
                        csilk_ctx_t*   c,
                        int64_t        user_id,
                        const char*    platform,
                        const char*    im_user_id,
                        csilk_json_t** out_data,
                        int*           out_code,
                        char*          err_buf,
                        size_t         err_cap);

int im_bot_usecase_bind_auth(void*          pool,
                             csilk_ctx_t*   c,
                             const char*    platform,
                             const char*    im_user_id,
                             const char*    username,
                             const char*    password,
                             csilk_json_t** out_data,
                             int*           out_code,
                             char*          err_buf,
                             size_t         err_cap);

csilk_json_t* im_bot_usecase_get_chat_task(void* pool, int64_t user_id, int64_t task_id);

int im_bot_usecase_chat_submit(void*          pool,
                               int64_t        user_id,
                               const char*    platform,
                               const char*    im_user_id,
                               const char*    message,
                               const char*    scope_override,
                               csilk_json_t** out_sync_data,
                               int64_t*       out_task_id,
                               bool*          out_deduplicated,
                               int64_t*       out_session_id,
                               int*           out_code,
                               char*          err_buf,
                               size_t         err_cap);
