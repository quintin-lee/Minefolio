/**
 * @file repository.h
 * @brief 报表领域仓储契约接口 (Domain Report Repository Contract)
 *
 * 报表域为只读查询域，仓储接口返回 JSON 结果。
 */

#pragma once

#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @brief 月度收支汇总（按分类、标签、每日）
 */
csilk_json_t* mf_report_repo_expense_monthly(void* pool, int64_t user_id, const char* date_pattern);

/**
 * @brief 收支趋势（按月聚合）
 */
csilk_json_t* mf_report_repo_expense_trend(void* pool, int64_t user_id, int months);

/**
 * @brief 年度收支汇总（按月分组）
 */
csilk_json_t* mf_report_repo_expense_yearly(void* pool, int64_t user_id, const char* year_str);

/**
 * @brief 按分类聚合支出分布
 */
csilk_json_t*
mf_report_repo_expense_category(void* pool, int64_t user_id, const char* period_pattern);

/**
 * @brief 按标签聚合支出分布
 */
csilk_json_t* mf_report_repo_expense_tag(void* pool, int64_t user_id, const char* period_pattern);

/**
 * @brief 资产价值趋势
 */
csilk_json_t* mf_report_repo_asset_trend(void* pool, int64_t user_id, int months);

/**
 * @brief 资产配置分布
 */
csilk_json_t* mf_report_repo_asset_breakdown(void* pool, int64_t user_id);

/**
 * @brief 交易表现/PnL（委托给 portfolio 域）
 */
int mf_report_repo_transaction_performance(void* pool, int64_t user_id, csilk_json_t** out_result);

/**
 * @brief 持仓明细（委托给 portfolio 域）
 */
int mf_report_repo_holdings(void* pool, int64_t user_id, csilk_json_t** out_result);

/**
 * @brief 资产汇总（总资产、总负债、净资产）
 */
csilk_json_t* mf_report_repo_asset_summary(void* pool, int64_t user_id);

/**
 * @brief 多币种资产分布
 */
csilk_json_t* mf_report_repo_multi_currency_summary(void* pool, int64_t user_id);

/**
 * @brief 外币资产汇率波动损益
 */
csilk_json_t* mf_report_repo_fx_pnl(void* pool, int64_t user_id);

/**
 * @brief Dashboard 综合总览（委托给 portfolio 域）
 */
int mf_report_repo_summary(void* pool, int64_t user_id, csilk_json_t** out_result);
