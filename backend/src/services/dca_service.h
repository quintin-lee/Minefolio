#pragma once
#include "csilk/csilk.h"

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
