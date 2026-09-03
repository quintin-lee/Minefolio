#pragma once

#include "csilk/csilk.h"

void api_cashflow_list_schedules(csilk_ctx_t* c);
void api_cashflow_create_schedule(csilk_ctx_t* c);
void api_cashflow_get_schedule(csilk_ctx_t* c);
void api_cashflow_update_schedule(csilk_ctx_t* c);
void api_cashflow_delete_schedule(csilk_ctx_t* c);
void api_cashflow_get_calendar(csilk_ctx_t* c);
void api_cashflow_confirm(csilk_ctx_t* c);

void register_cashflow_routes(csilk_app_t* app);
