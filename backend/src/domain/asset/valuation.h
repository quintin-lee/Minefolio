#pragma once

#include "core/financial/currency.h"
#include "core/financial/money.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief 当前市场估值值对象 (Valuation Value Object)
 */
typedef struct {
    price_t    current_price;   /**< 最新单位价格/净值 */
    money_t    market_value;    /**< 当前市场总价值 = quantity * current_price */
    char       as_of[32];       /**< 估值基准时点 */
    char       price_source[32];/**< 价格来源 */
} mf_valuation_t;

/**
 * @brief 根据份额和单价计算市场估值
 */
static inline mf_valuation_t mf_valuation_calculate(quantity_t  quantity,
                                                    price_t     current_price,
                                                    const char* as_of,
                                                    const char* source) {
    mf_valuation_t val;
    memset(&val, 0, sizeof(val));
    val.current_price = current_price;
    money_t mv;
    if (price_times_quantity(current_price, quantity, &mv) == DECIMAL_OK) {
        val.market_value = mv;
    } else {
        val.market_value = money_zero(current_price.currency);
    }
    if (as_of) {
        snprintf(val.as_of, sizeof(val.as_of), "%s", as_of);
    }
    if (source) {
        snprintf(val.price_source, sizeof(val.price_source), "%s", source);
    }
    return val;
}
