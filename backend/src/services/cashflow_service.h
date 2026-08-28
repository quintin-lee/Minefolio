#pragma once
#include "csilk/csilk.h"

void cashflow_service_list_schedules(csilk_ctx_t* c);
void cashflow_service_create_schedule(csilk_ctx_t* c);
void cashflow_service_get_schedule(csilk_ctx_t* c);
void cashflow_service_update_schedule(csilk_ctx_t* c);
void cashflow_service_delete_schedule(csilk_ctx_t* c);
void cashflow_service_get_calendar(csilk_ctx_t* c);
void cashflow_service_confirm(csilk_ctx_t* c);
