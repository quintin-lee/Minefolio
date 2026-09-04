/**
 * @file report_controller.h
 * @brief 统计报表、持仓分析与仪表盘数据控制器头文件 (DDD 接口层)
 */

#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @brief 获取月度收支明细分类报表 (GET /api/reports/expense/monthly)
 */
void report_expense_monthly(csilk_ctx_t* c);

/**
 * @brief 获取近 N 个月收支变动趋势 (GET /api/reports/expense/trend)
 */
void report_expense_trend(csilk_ctx_t* c);

/**
 * @brief 获取年度收支汇总报表 (GET /api/reports/expense/yearly)
 */
void report_expense_yearly(csilk_ctx_t* c);

/**
 * @brief 获取按分类聚合的支出分布统计 (GET /api/reports/expense/category)
 */
void report_expense_category(csilk_ctx_t* c);

/**
 * @brief 获取按标签聚合的支出统计 (GET /api/reports/expense/tag)
 */
void report_expense_tag(csilk_ctx_t* c);

/**
 * @brief 获取历史资产与净资产走势趋势 (GET /api/reports/asset/trend)
 */
void report_asset_trend(csilk_ctx_t* c);

/**
 * @brief 获取资产配置大类占比分布 (GET /api/reports/asset/breakdown)
 */
void report_asset_breakdown(csilk_ctx_t* c);

/**
 * @brief 获取投资交易历史表现与综合 PnL 盈亏报表 (GET /api/reports/transaction/performance)
 */
void report_transaction_performance(csilk_ctx_t* c);

/**
 * @brief 获取当前证券/基金/加密货币投资持仓明细报表 (GET /api/reports/holdings)
 */
void report_holdings(csilk_ctx_t* c);

/**
 * @brief 获取资产汇总概览（总资产、总负债与净资产） (GET /api/reports/asset/summary)
 */
void report_asset_summary(csilk_ctx_t* c);

/**
 * @brief 获取多币种资产分布与折算净资产汇总 (GET /api/reports/multi-currency-summary)
 */
void report_multi_currency_summary(csilk_ctx_t* c);

/**
 * @brief 获取外币资产汇率波动损益与标的收益归因报表 (GET /api/reports/fx-pnl)
 */
void report_fx_pnl(csilk_ctx_t* c);

/**
 * @brief 获取首页 Dashboard 综合总览数据 (GET /api/summary)
 */
void summary_get(csilk_ctx_t* c);

/**
 * @brief 注册统计报表与数据看板相关的所有 HTTP 路由
 */
void register_report_routes(csilk_app_t* app);
