/**
 * @file usecases.h
 * @brief 报表用例接口声明 (Application Layer)
 *
 * 报表域的用例层将跨域依赖隔离在应用层，
 * domain 层定义报表实体和仓储契约，
 * infrastructure 层实现仓储（包装现有 service 函数）。
 */

#pragma once

#include "csilk/csilk.h"

/**
 * @brief 月度收支报表用例
 */
void report_usecase_expense_monthly(csilk_ctx_t* c);

/**
 * @brief 收支趋势报表用例
 */
void report_usecase_expense_trend(csilk_ctx_t* c);

/**
 * @brief 年度收支报表用例
 */
void report_usecase_expense_yearly(csilk_ctx_t* c);

/**
 * @brief 分类支出分布用例
 */
void report_usecase_expense_category(csilk_ctx_t* c);

/**
 * @brief 标签支出统计用例
 */
void report_usecase_expense_tag(csilk_ctx_t* c);

/**
 * @brief 资产走势报表用例
 */
void report_usecase_asset_trend(csilk_ctx_t* c);

/**
 * @brief 资产配置分布用例
 */
void report_usecase_asset_breakdown(csilk_ctx_t* c);

/**
 * @brief 投资表现报表用例（依赖 portfolio 域）
 */
void report_usecase_transaction_performance(csilk_ctx_t* c);

/**
 * @brief 持仓明细报表用例（依赖 portfolio 域）
 */
void report_usecase_holdings(csilk_ctx_t* c);

/**
 * @brief 资产汇总报表用例
 */
void report_usecase_asset_summary(csilk_ctx_t* c);

/**
 * @brief 多币种汇总报表用例（依赖 market 域）
 */
void report_usecase_multi_currency_summary(csilk_ctx_t* c);

/**
 * @brief 汇率损益报表用例（依赖 market 域）
 */
void report_usecase_fx_pnl(csilk_ctx_t* c);

/**
 * @brief Dashboard 汇总用例（依赖 portfolio 域）
 */
void report_usecase_dashboard_summary(csilk_ctx_t* c);
