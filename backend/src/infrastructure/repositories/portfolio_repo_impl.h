#pragma once

#include "domain/portfolio/repository.h"
#include "csilk/csilk.h"

/**
 * @brief 查询资产分类聚合市值与币种（排除负债）
 */
csilk_json_t* portfolio_repo_get_category_assets(void* pool, int64_t user_id, int64_t ledger_id);

/**
 * @brief 查询负债类资产市值与币种
 */
csilk_json_t* portfolio_repo_get_liability_assets(void* pool, int64_t user_id, int64_t ledger_id);

/**
 * @brief 查询最近 N 笔交易流水
 */
csilk_json_t*
portfolio_repo_get_recent_transactions(void* pool, int64_t user_id, int64_t ledger_id, int limit);

/**
 * @brief 查询用于 30 天趋势计算的资产基础数据
 */
csilk_json_t* portfolio_repo_get_trend_assets(void* pool, int64_t user_id, int64_t ledger_id);

/**
 * @brief 查询用于 30 天趋势计算的余额变动记录
 */
csilk_json_t* portfolio_repo_get_trend_balance_logs(void*       pool,
                                                    int64_t     user_id,
                                                    int64_t     ledger_id,
                                                    const char* start_date);

/**
 * @brief 查询交易表现中的全部关联交易
 */
csilk_json_t*
portfolio_repo_get_performance_transactions(void* pool, int64_t user_id, int64_t ledger_id);

/**
 * @brief 查询投资类资产持仓原始成本与市值
 */
csilk_json_t* portfolio_repo_get_holdings_raw(void* pool, int64_t user_id, int64_t ledger_id);
