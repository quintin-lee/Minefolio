#pragma once

#include "domain/portfolio/repository.h"
#include "csilk/csilk.h"

/**
 * @brief 查询资产分类聚合市值与币种（排除负债）
 */
csilk_json_t* portfolio_repo_get_category_assets(void* pool, int64_t user_id);

/**
 * @brief 查询用户总负债额
 */
double portfolio_repo_get_total_liabilities(void* pool, int64_t user_id);

/**
 * @brief 查询最近 N 笔交易流水
 */
csilk_json_t* portfolio_repo_get_recent_transactions(void* pool, int64_t user_id, int limit);

/**
 * @brief 查询 30 天资产负债趋势
 */
csilk_json_t* portfolio_repo_get_trend_30d(void* pool, int64_t user_id);

/**
 * @brief 查询交易表现中的全部关联交易
 */
csilk_json_t* portfolio_repo_get_performance_transactions(void* pool, int64_t user_id);

/**
 * @brief 查询当前投资类资产持仓总市值与总成本
 */
int portfolio_repo_get_current_holdings_totals(void* pool, int64_t user_id,
                                              double* out_qty, double* out_cost, double* out_market);
