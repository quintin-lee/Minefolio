#pragma once

#include "domain/market/repository.h"
#include "csilk/csilk.h"

/* 基础设施层：基于 SQL/SQLite 实现市场行情及汇率仓储契约 */

/**
 * @brief 查询指定资产的历史价格走势记录列表
 */
csilk_json_t*
mf_market_repo_price_history_list(void* pool, int64_t user_id, int64_t asset_id, int limit);

/**
 * @brief 从数据库查询大盘指数记录列表 (只读)
 * @param pool 数据库连接池
 * @param category 分类过滤 ("all" 或 NULL 表示全量)
 * @return csilk_json_t* JSON 数组
 */
csilk_json_t* mf_market_repo_get_indices(void* pool, const char* category);

/**
 * @brief Upsert 写入或更新单条大盘指数记录到数据库
 */
int mf_market_repo_upsert_index(void*       pool,
                                const char* symbol,
                                const char* name,
                                const char* category,
                                const char* region_flag,
                                double      current_price,
                                double      change_amount,
                                double      change_percent,
                                double      prev_close,
                                double      open_price,
                                double      high_price,
                                double      low_price,
                                const char* currency,
                                const char* quote_time,
                                const char* sparkline_json);

/**
 * @brief 从数据库直接查询所有基础货币为 CNY 的实时外汇汇率映射
 * @return csilk_json_t* JSON 对象，形如 {"CNY": 1.0, "USD": 6.6881, ...}
 */
csilk_json_t* mf_market_repo_get_all_exchange_rates(void* pool);
