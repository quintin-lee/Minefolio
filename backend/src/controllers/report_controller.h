#pragma once
#include "csilk/csilk.h"
void report_expense_monthly(csilk_ctx_t* c);
void report_expense_trend(csilk_ctx_t* c);
void report_expense_yearly(csilk_ctx_t* c);
void report_expense_category(csilk_ctx_t* c);
void report_expense_tag(csilk_ctx_t* c);
void report_asset_trend(csilk_ctx_t* c);
void report_asset_breakdown(csilk_ctx_t* c);
void report_transaction_performance(csilk_ctx_t* c);
void report_holdings(csilk_ctx_t* c);
void report_asset_summary(csilk_ctx_t* c);
void summary_get(csilk_ctx_t* c);
