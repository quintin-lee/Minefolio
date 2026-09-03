#pragma once

#include "csilk/csilk.h"
#include "application/ai/commands.h"
#include "application/ai/dtos.h"

int ai_usecase_models_list(csilk_json_t** out_data, ai_usecase_result_t* out_res);

int ai_usecase_sessions_list(void*                pool,
                             int64_t              user_id,
                             int64_t              page,
                             int64_t              page_size,
                             csilk_json_t**       out_data,
                             int64_t*             out_total,
                             ai_usecase_result_t* out_res);

int ai_usecase_sessions_create(void*                          pool,
                               const ai_create_session_cmd_t* cmd,
                               int64_t*                       out_id,
                               ai_usecase_result_t*           out_res);

int ai_usecase_sessions_get(
    void* pool, int64_t user_id, int64_t id, csilk_json_t** out_data, ai_usecase_result_t* out_res);

int ai_usecase_sessions_update(void*                          pool,
                               const ai_update_session_cmd_t* cmd,
                               ai_usecase_result_t*           out_res);

int
ai_usecase_sessions_delete(void* pool, int64_t user_id, int64_t id, ai_usecase_result_t* out_res);

int ai_usecase_messages_list(void*                pool,
                             int64_t              user_id,
                             int64_t              session_id,
                             int64_t              page,
                             int64_t              page_size,
                             csilk_json_t**       out_data,
                             int64_t*             out_total,
                             ai_usecase_result_t* out_res);

int ai_usecase_settings_get(csilk_json_t** out_data, ai_usecase_result_t* out_res);

int ai_usecase_settings_update(void* pool, const csilk_json_t* body, ai_usecase_result_t* out_res);

int ai_usecase_workflows_list(csilk_json_t** out_data, ai_usecase_result_t* out_res);

int ai_usecase_trace_list(void*                pool,
                          int64_t              user_id,
                          int64_t              page,
                          int64_t              page_size,
                          const char*          provider,
                          const char*          model,
                          csilk_json_t**       out_data,
                          int64_t*             out_total,
                          ai_usecase_result_t* out_res);

int ai_usecase_trace_stats(void*                pool,
                           int64_t              user_id,
                           csilk_json_t**       out_data,
                           ai_usecase_result_t* out_res);

int ai_usecase_trace_get(
    void* pool, int64_t user_id, int64_t id, csilk_json_t** out_data, ai_usecase_result_t* out_res);
