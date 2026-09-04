#pragma once

#include "csilk/csilk.h"
#include "csilk/app/app.h"

void api_dca_list_plans(csilk_ctx_t* c);
void api_dca_create_plan(csilk_ctx_t* c);
void api_dca_get_plan(csilk_ctx_t* c);
void api_dca_update_plan(csilk_ctx_t* c);
void api_dca_set_plan_status(csilk_ctx_t* c);
void api_dca_delete_plan(csilk_ctx_t* c);
void api_dca_list_executions(csilk_ctx_t* c);
void api_dca_list_pending_executions(csilk_ctx_t* c);
void api_dca_confirm_execution(csilk_ctx_t* c);
void api_dca_skip_execution(csilk_ctx_t* c);

// Backward-compatibility aliases
void dca_service_list_plans(csilk_ctx_t* c);
void dca_service_create_plan(csilk_ctx_t* c);
void dca_service_get_plan(csilk_ctx_t* c);
void dca_service_update_plan(csilk_ctx_t* c);
void dca_service_set_plan_status(csilk_ctx_t* c);
void dca_service_delete_plan(csilk_ctx_t* c);
void dca_service_list_executions(csilk_ctx_t* c);
void dca_service_list_pending_executions(csilk_ctx_t* c);
void dca_service_confirm_execution(csilk_ctx_t* c);
void dca_service_skip_execution(csilk_ctx_t* c);

void register_dca_routes(csilk_app_t* app);
