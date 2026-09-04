#pragma once

#include "core/financial/currency.h"
#include "core/financial/money.h"

/**
 * @brief 损益指标值对象 (PnL Value Object)
 * 严格区分已实现盈亏与未实现浮动盈亏
 */
typedef struct {
    money_t realized_pnl;     /**< 历史累计已实现盈亏 (平仓净价差 + 现金分红) */
    money_t unrealized_pnl;   /**< 账面浮动盈亏 = market_value - total_cost */
    money_t total_pnl;        /**< 综合总盈亏 = realized_pnl + unrealized_pnl */
    double  unrealized_pct;   /**< 账面浮动收益率 (%) */
    double  total_return_pct; /**< 综合总回报率 (%) */
} mf_pnl_t;

/**
 * @brief 计算损益指标
 */
mf_pnl_t mf_pnl_calculate(money_t cost_basis,
                          money_t market_value,
                          money_t realized_pnl);
