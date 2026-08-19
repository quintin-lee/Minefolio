#pragma once
#include "csilk/csilk.h"
void daily_expenses_list(csilk_ctx_t* c);
void daily_expenses_create(csilk_ctx_t* c);
void daily_expenses_update(csilk_ctx_t* c);
void daily_expenses_delete(csilk_ctx_t* c);
void daily_expenses_monthly(csilk_ctx_t* c);
#include "csilk/app/app.h"
void register_daily_expense_routes(csilk_app_t* app);
