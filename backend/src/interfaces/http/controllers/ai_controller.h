#pragma once

#include "csilk/csilk.h"

void api_ai_models_handler(csilk_ctx_t* c);
void api_ai_chat_handler(csilk_ctx_t* c);
void api_ai_sessions_list_handler(csilk_ctx_t* c);
void api_ai_sessions_create_handler(csilk_ctx_t* c);
void api_ai_sessions_get_handler(csilk_ctx_t* c);
void api_ai_sessions_update_handler(csilk_ctx_t* c);
void api_ai_sessions_delete_handler(csilk_ctx_t* c);
void api_ai_messages_list_handler(csilk_ctx_t* c);
void api_ai_settings_get_handler(csilk_ctx_t* c);
void api_ai_settings_update_handler(csilk_ctx_t* c);
void api_ai_test_connection_handler(csilk_ctx_t* c);
void api_ai_fetch_models_handler(csilk_ctx_t* c);
void api_ai_workflows_list_handler(csilk_ctx_t* c);
void api_ai_workflows_run_handler(csilk_ctx_t* c);

void register_ai_routes(csilk_app_t* app);
