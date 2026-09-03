#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "domain/market/entity.h"

/**
 * @brief 计算行情同步带来的资产持仓总市值差量：delta = (new_price - old_price) * quantity
 */
int mf_market_rule_calc_sync_delta(price_t old_price, price_t new_price, quantity_t qty,
                                   currency_t cur, money_t* out_delta);

/**
 * @brief 外币金额按汇率换算为基准币种金额
 */
int mf_market_rule_convert_currency(money_t src, double rate_to_cny,
                                    currency_t cny_currency, money_t* out_cny);

/**
 * @brief 校验市场行情报价快照数据的有效性
 */
int mf_market_rule_validate_quote(const mf_market_quote_t* q, char* err_buf, size_t err_len);

/**
 * @brief 校验市场配置参数有效性
 */
int mf_market_rule_validate_settings(const mf_market_settings_t* s, char* err_buf, size_t err_len);
