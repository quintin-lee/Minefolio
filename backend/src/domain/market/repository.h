#pragma once

#include <stdint.h>
#include <stddef.h>
#include "domain/market/entity.h"

/**
 * @brief 市场行情与汇率仓储抽象契约接口 (Domain Market Repository Contract)
 * @note 纯 C 契约，严格禁止依赖 HTTP 框架或直接编写 SQL
 */

/**
 * @brief 更新或保存外币对 CNY 汇率
 */
int mf_market_repo_save_exchange_rate(void* pool, const char* currency, double rate);

/**
 * @brief 记录资产最新价格历史快照 (K 线)
 */
int mf_market_repo_record_price_history(
    void* pool, int64_t asset_id, const char* date, price_t price, currency_t cur);

/**
 * @brief 更新资产的最新市场行情净值/价格
 */
int mf_market_repo_update_asset_quote(void* pool, int64_t user_id, int64_t asset_id, price_t price);
