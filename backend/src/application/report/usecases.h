/**
 * @file usecases.h
 * @brief 报表用例接口声明 (Application Layer)
 *
 * 报表域的用例层将跨域依赖隔离在应用层：
 * 用例函数签名统一使用 `void* pool` + `report_query_t` + `report_result_t*`，
 * 不直接依赖 csilk HTTP 框架类型（`csilk_ctx_t`）。
 * 参数解析与响应封装由 controller 层负责（见 interfaces/http/controllers/report_controller.c）。
 */

#pragma once

#include "application/report/commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 收支报表用例 ───────────────────────────────────────────────── */

/**
 * @brief 月度收支报表用例
 * @return 0 成功；<0 用例内部错误（out_res->code 携带具体业务码）
 */
int report_usecase_expense_monthly(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 收支趋势报表用例
 */
int report_usecase_expense_trend(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 年度收支报表用例
 */
int report_usecase_expense_yearly(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 分类支出分布用例
 */
int report_usecase_expense_category(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 标签支出统计用例
 */
int report_usecase_expense_tag(void* pool, const report_query_t* q, report_result_t* out_res);

/* ── 资产报表用例 ──────────────────────────────────────────────── */

/**
 * @brief 资产走势报表用例
 */
int report_usecase_asset_trend(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 资产配置分布用例
 */
int report_usecase_asset_breakdown(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 资产汇总报表用例
 */
int report_usecase_asset_summary(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 多币种汇总报表用例（依赖 market 域）
 */
int report_usecase_multi_currency_summary(void*                 pool,
                                          const report_query_t* q,
                                          report_result_t*      out_res);

/**
 * @brief 汇率损益报表用例（依赖 market 域）
 */
int report_usecase_fx_pnl(void* pool, const report_query_t* q, report_result_t* out_res);

#ifdef __cplusplus
}
#endif
