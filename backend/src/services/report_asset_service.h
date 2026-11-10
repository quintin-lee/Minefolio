#pragma once
#include "application/report/commands.h"

/**
 * @brief 资产走势报表
 */
void report_asset_trend(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 资产配置分布
 */
void report_asset_breakdown(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 资产汇总报表
 */
void report_asset_summary(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 多币种资产分布与折算净资产汇总（依赖 market 域）
 */
void report_multi_currency_summary(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 外币资产汇率波动损益与标的收益归因（依赖 market 域）
 */
void report_fx_pnl(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 持仓总览（当日收益 + 累计总收益 + HHI 集中度 + 资产配置分布）
 */
void report_position_overview(void* pool, const report_query_t* q, report_result_t* out_res);
