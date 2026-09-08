/**
 * @file usecases.c
 * @brief 报表用例实现 (Application Layer)
 *
 * 用例层将跨域依赖隔离，委托给现有的 service 函数执行。
 * 这些 service 函数作为 infrastructure 实现层使用。
 */

#include "application/report/usecases.h"
#include "services/report_expense_service.h"
#include "services/report_asset_service.h"
#include "services/report_holdings_service.h"

/* ── 收报表用例 ────────────────────────────────────────────────── */

void
report_usecase_expense_monthly(csilk_ctx_t* c)
{
    report_expense_monthly(c);
}

void
report_usecase_expense_trend(csilk_ctx_t* c)
{
    report_expense_trend(c);
}

void
report_usecase_expense_yearly(csilk_ctx_t* c)
{
    report_expense_yearly(c);
}

void
report_usecase_expense_category(csilk_ctx_t* c)
{
    report_expense_category(c);
}

void
report_usecase_expense_tag(csilk_ctx_t* c)
{
    report_expense_tag(c);
}

/* ── 资产报表用例 ────────────────────────────────────────────────── */

void
report_usecase_asset_trend(csilk_ctx_t* c)
{
    report_asset_trend(c);
}

void
report_usecase_asset_breakdown(csilk_ctx_t* c)
{
    report_asset_breakdown(c);
}

void
report_usecase_transaction_performance(csilk_ctx_t* c)
{
    report_transaction_performance(c);
}

void
report_usecase_holdings(csilk_ctx_t* c)
{
    report_holdings(c);
}

void
report_usecase_asset_summary(csilk_ctx_t* c)
{
    report_asset_summary(c);
}

void
report_usecase_multi_currency_summary(csilk_ctx_t* c)
{
    report_multi_currency_summary(c);
}

void
report_usecase_fx_pnl(csilk_ctx_t* c)
{
    report_fx_pnl(c);
}

void
report_usecase_dashboard_summary(csilk_ctx_t* c)
{
    summary_get(c);
}
