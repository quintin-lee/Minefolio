/**
 * @file report_controller.c
 * @brief 统计报表、持仓分析与仪表盘数据控制器实现 (DDD 接口层)
 *
 * 路由处理器均解析 HTTP 上下文（user/ledger/query 参数），填充 report_query_t，
 * 委托 application/report 用例或 application/portfolio 用例执行，
 * 并通过 common/response.h 封装 {code, message, data} 响应信封。
 */

#include "interfaces/http/controllers/report_controller.h"
#include "application/report/usecases.h"
#include "application/portfolio/usecases.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/response.h"
#include "csilk/csilk.h"
#include <stdlib.h>

/* ── 公共解析与响应辅助 ───────────────────────────────────────── */

/**
 * @brief 解析 user/ledger 并填充 report_query_t 的 user_id/ledger_id 字段
 * @return 0 成功（q->user_id/ledger_id 已填充）；非 0 表示已发送错误响应
 */
static int
report_ctx_resolve(csilk_ctx_t* c, report_query_t* q)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return -1;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return -1;
    }
    q->user_id = user_id;
    q->ledger_id = ledger_id;
    return 0;
}

static void
report_ctx_resolve_year_month(csilk_ctx_t* c, report_query_t* q)
{
    q->year = csilk_get_query(c, "year");
    q->month = csilk_get_query(c, "month");
}

static void
report_ctx_resolve_days(csilk_ctx_t* c, report_query_t* q)
{
    const char* s = csilk_get_query(c, "months");
    q->days = s ? atoi(s) : 0;
}

static void
report_ctx_resolve_period(csilk_ctx_t* c, report_query_t* q)
{
    q->period = csilk_get_query(c, "period");
}

static void
report_ctx_resolve_base_currency(csilk_ctx_t* c, report_query_t* q)
{
    q->base_currency = csilk_get_query(c, "base_currency");
}

/**
 * @brief 封装 report 用例结果到响应信封
 * @param out_res  结果对象（code==0 时 data_payload 被挂载并释放；非 0 时 data_payload 须为 NULL）
 * @param owned    true 表示 data_payload 为本次调用临时构造的所有权，需在此释放；false 表示由调用方持有
 */
static void
report_respond(csilk_ctx_t* c, const report_result_t* out_res, bool owned)
{
    if (out_res->code == 0) {
        if (owned) {
            respond_ok(c, out_res->data_payload);
            csilk_json_free(out_res->data_payload);
        } else {
            respond_ok(c, out_res->data_payload);
        }
    } else {
        respond_error(c, out_res->code, out_res->message);
    }
}

/* ── 报表 HTTP 处理器 ──────────────────────────────────────────── */

void
report_expense_monthly_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_ctx_resolve_year_month(c, &q);
    report_usecase_expense_monthly(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

void
report_expense_trend_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_ctx_resolve_days(c, &q);
    report_usecase_expense_trend(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

void
report_expense_yearly_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_ctx_resolve_year_month(c, &q);
    report_usecase_expense_yearly(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

void
report_expense_category_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_ctx_resolve_year_month(c, &q);
    report_usecase_expense_category(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

void
report_expense_tag_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_ctx_resolve_year_month(c, &q);
    report_usecase_expense_tag(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

void
report_asset_trend_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_ctx_resolve_period(c, &q);
    report_usecase_asset_trend(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

void
report_asset_breakdown_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_usecase_asset_breakdown(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

void
report_asset_summary_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_usecase_asset_summary(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

void
report_multi_currency_summary_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_ctx_resolve_base_currency(c, &q);
    report_usecase_multi_currency_summary(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

void
report_fx_pnl_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_ctx_resolve_base_currency(c, &q);
    report_usecase_fx_pnl(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

/* ── 持仓/投资表现/仪表盘：委托 portfolio 域用例 ─────────────── */

void
report_holdings_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }

    const char*           req_cur = csilk_get_query(c, "currency");
    query_portfolio_cmd_t cmd = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .base_currency = (req_cur && req_cur[0]) ? req_cur : "CNY",
    };

    csilk_json_t*              resp = NULL;
    portfolio_usecase_result_t res = {0};
    int rc = portfolio_usecase_get_holdings(db_get_pool(), &cmd, &resp, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, resp);
        csilk_json_free(resp);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "查询失败");
    }
}

void
report_transaction_performance_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }

    query_portfolio_cmd_t cmd = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .base_currency = "CNY",
    };

    csilk_json_t*              resp = NULL;
    portfolio_usecase_result_t res = {0};
    int rc = portfolio_usecase_get_performance(db_get_pool(), &cmd, &resp, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, resp);
        csilk_json_free(resp);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "查询失败");
    }
}

void
report_dashboard_summary_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }

    query_portfolio_cmd_t cmd = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .base_currency = "CNY",
    };

    csilk_json_t*              resp = NULL;
    portfolio_usecase_result_t res = {0};
    int rc = portfolio_usecase_get_dashboard_summary(db_get_pool(), &cmd, &resp, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, resp);
        csilk_json_free(resp);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "查询失败");
    }
}

/* ── 路由注册 ──────────────────────────────────────────────────── */

void
report_position_overview_handler(csilk_ctx_t* c)
{
    report_query_t  q = {0};
    report_result_t res = {0};
    if (report_ctx_resolve(c, &q) != 0) {
        return;
    }
    report_usecase_position_overview(db_get_pool(), &q, &res);
    report_respond(c, &res, true);
}

/* ── 路由注册 ──────────────────────────────────────────────────── */

void
register_report_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/reports/expense/monthly",
                      report_expense_monthly_handler,
                      nullptr,
                      nullptr,
                      "Monthly expense report",
                      "Returns monthly income/expense breakdown by category and tag");
    csilk_app_get_ext(app,
                      "/api/reports/expense/trend",
                      report_expense_trend_handler,
                      nullptr,
                      nullptr,
                      "Expense trend",
                      "Returns expense trend over N months (query param: months)");
    csilk_app_get_ext(app,
                      "/api/reports/expense/yearly",
                      report_expense_yearly_handler,
                      nullptr,
                      nullptr,
                      "Yearly expense report",
                      "Returns yearly expense summary grouped by month");
    csilk_app_get_ext(app,
                      "/api/reports/expense/category",
                      report_expense_category_handler,
                      nullptr,
                      nullptr,
                      "Expense by category",
                      "Returns expense totals grouped by category");
    csilk_app_get_ext(app,
                      "/api/reports/expense/tag",
                      report_expense_tag_handler,
                      nullptr,
                      nullptr,
                      "Expense by tag",
                      "Returns expense totals grouped by tag");
    csilk_app_get_ext(app,
                      "/api/reports/asset/trend",
                      report_asset_trend_handler,
                      nullptr,
                      nullptr,
                      "Asset value trend",
                      "Returns asset value trend over time (query param: period)");
    csilk_app_get_ext(app,
                      "/api/reports/asset/breakdown",
                      report_asset_breakdown_handler,
                      nullptr,
                      nullptr,
                      "Asset breakdown",
                      "Returns asset allocation breakdown by category");
    csilk_app_get_ext(app,
                      "/api/reports/transaction/performance",
                      report_transaction_performance_handler,
                      nullptr,
                      nullptr,
                      "Transaction performance",
                      "Returns investment transaction performance/PnL report");
    csilk_app_get_ext(app,
                      "/api/reports/holdings",
                      report_holdings_handler,
                      nullptr,
                      nullptr,
                      "Portfolio holdings",
                      "Returns current portfolio holdings for investment assets");
    csilk_app_get_ext(app,
                      "/api/reports/asset/summary",
                      report_asset_summary_handler,
                      nullptr,
                      nullptr,
                      "Asset summary",
                      "Returns aggregated asset summary including net worth");
    csilk_app_get_ext(
        app,
        "/api/reports/multi-currency-summary",
        report_multi_currency_summary_handler,
        nullptr,
        nullptr,
        "Multi-currency summary",
        "Returns multi-currency asset breakdown and converted net worth in base currency");
    csilk_app_get_ext(app,
                      "/api/reports/currency-summary",
                      report_multi_currency_summary_handler,
                      nullptr,
                      nullptr,
                      "Currency summary alias",
                      "Alias for multi-currency summary");
    csilk_app_get_ext(app,
                      "/api/reports/fx-pnl",
                      report_fx_pnl_handler,
                      nullptr,
                      nullptr,
                      "FX Gain/Loss report",
                      "Returns foreign currency asset returns decomposed into price gain and FX "
                      "fluctuation gain");
    csilk_app_get_ext(
        app,
        "/api/summary",
        report_dashboard_summary_handler,
        nullptr,
        nullptr,
        "Dashboard summary",
        "Returns dashboard aggregate: net worth, monthly income/expense, recent transactions");
    csilk_app_get_ext(
        app,
        "/api/reports/position-overview",
        report_position_overview_handler,
        nullptr,
        nullptr,
        "Position overview",
        "Returns daily P&L, total P&L, HHI concentration, and asset category breakdown");
}
