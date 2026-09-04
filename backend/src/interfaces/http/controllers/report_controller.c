/**
 * @file report_controller.c
 * @brief 统计报表、持仓分析与仪表盘数据控制器实现 (DDD 接口层)
 */

#include "interfaces/http/controllers/report_controller.h"
#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include "services/report_expense_service.h"
#include "services/report_asset_service.h"
#include "services/report_holdings_service.h"

void
register_report_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/reports/expense/monthly",
                      report_expense_monthly,
                      nullptr,
                      nullptr,
                      "Monthly expense report",
                      "Returns monthly income/expense breakdown by category and tag");
    csilk_app_get_ext(app,
                      "/api/reports/expense/trend",
                      report_expense_trend,
                      nullptr,
                      nullptr,
                      "Expense trend",
                      "Returns expense trend over N months (query param: months)");
    csilk_app_get_ext(app,
                      "/api/reports/expense/yearly",
                      report_expense_yearly,
                      nullptr,
                      nullptr,
                      "Yearly expense report",
                      "Returns yearly expense summary grouped by month");
    csilk_app_get_ext(app,
                      "/api/reports/expense/category",
                      report_expense_category,
                      nullptr,
                      nullptr,
                      "Expense by category",
                      "Returns expense totals grouped by category");
    csilk_app_get_ext(app,
                      "/api/reports/expense/tag",
                      report_expense_tag,
                      nullptr,
                      nullptr,
                      "Expense by tag",
                      "Returns expense totals grouped by tag");
    csilk_app_get_ext(app,
                      "/api/reports/asset/trend",
                      report_asset_trend,
                      nullptr,
                      nullptr,
                      "Asset value trend",
                      "Returns asset value trend over time (query param: months)");
    csilk_app_get_ext(app,
                      "/api/reports/asset/breakdown",
                      report_asset_breakdown,
                      nullptr,
                      nullptr,
                      "Asset breakdown",
                      "Returns asset allocation breakdown by category");
    csilk_app_get_ext(app,
                      "/api/reports/transaction/performance",
                      report_transaction_performance,
                      nullptr,
                      nullptr,
                      "Transaction performance",
                      "Returns investment transaction performance/PnL report");
    csilk_app_get_ext(app,
                      "/api/reports/holdings",
                      report_holdings,
                      nullptr,
                      nullptr,
                      "Portfolio holdings",
                      "Returns current portfolio holdings for investment assets");
    csilk_app_get_ext(app,
                      "/api/reports/asset/summary",
                      report_asset_summary,
                      nullptr,
                      nullptr,
                      "Asset summary",
                      "Returns aggregated asset summary including net worth");
    csilk_app_get_ext(
        app,
        "/api/reports/multi-currency-summary",
        report_multi_currency_summary,
        nullptr,
        nullptr,
        "Multi-currency summary",
        "Returns multi-currency asset breakdown and converted net worth in base currency");
    csilk_app_get_ext(app,
                      "/api/reports/currency-summary",
                      report_multi_currency_summary,
                      nullptr,
                      nullptr,
                      "Currency summary alias",
                      "Alias for multi-currency summary");
    csilk_app_get_ext(app,
                      "/api/reports/fx-pnl",
                      report_fx_pnl,
                      nullptr,
                      nullptr,
                      "FX Gain/Loss report",
                      "Returns foreign currency asset returns decomposed into price gain and FX "
                      "fluctuation gain");
    csilk_app_get_ext(
        app,
        "/api/summary",
        summary_get,
        nullptr,
        nullptr,
        "Dashboard summary",
        "Returns dashboard aggregate: net worth, monthly income/expense, recent transactions");
}
