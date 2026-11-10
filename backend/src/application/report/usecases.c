/**
 * @file usecases.c
 * @brief 报表用例实现 (Application Layer)
 *
 * 用例层将跨域依赖隔离，委托给 report service 层（纯数据契约，无 HTTP 依赖）
 * 与 portfolio 域用例执行。controller 负责从 csilk_ctx_t 解析 user/ledger/query
 * 参数并填充 report_query_t，响应层封装 out_res->data_payload。
 */

#include "application/report/usecases.h"
#include "services/report_expense_service.h"
#include "services/report_asset_service.h"
#include <string.h>

/* ── 收支报表用例 ────────────────────────────────────────────────── */

int
report_usecase_expense_monthly(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_expense_monthly(pool, q, out_res);
    return 0;
}

int
report_usecase_expense_trend(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_expense_trend(pool, q, out_res);
    return 0;
}

int
report_usecase_expense_yearly(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_expense_yearly(pool, q, out_res);
    return 0;
}

int
report_usecase_expense_category(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_expense_category(pool, q, out_res);
    return 0;
}

int
report_usecase_expense_tag(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_expense_tag(pool, q, out_res);
    return 0;
}

/* ── 资产报表用例 ──────────────────────────────────────────────── */

int
report_usecase_asset_trend(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_asset_trend(pool, q, out_res);
    return 0;
}

int
report_usecase_asset_breakdown(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_asset_breakdown(pool, q, out_res);
    return 0;
}

int
report_usecase_asset_summary(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_asset_summary(pool, q, out_res);
    return 0;
}

int
report_usecase_multi_currency_summary(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_multi_currency_summary(pool, q, out_res);
    return 0;
}

int
report_usecase_fx_pnl(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_fx_pnl(pool, q, out_res);
    return 0;
}

int
report_usecase_position_overview(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    report_position_overview(pool, q, out_res);
    return 0;
}
