#pragma once
#include "csilk/csilk.h"
#include "common/ai_config.h"

void         ai_init(csilk_db_pool_t* pool);
void         ai_shutdown(void);
void         ai_chat_handler(csilk_ctx_t* c);
void         ai_service_test_connection(csilk_ctx_t* c);
void         ai_service_fetch_models(csilk_ctx_t* c);
ai_config_t* ai_get_config(void);
int          ai_service_stream_report(csilk_ctx_t* c,
                                      int64_t      user_id,
                                      int64_t      session_id,
                                      const char*  workflow_title,
                                      const char*  structured_data_json);
