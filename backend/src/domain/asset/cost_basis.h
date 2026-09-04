#pragma once

#include "core/financial/currency.h"
#include "core/financial/money.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"

/**
 * @brief 成本基础值对象 (Cost Basis Value Object)
 * 严格管理持仓份额、加权总成本、盈亏净成本与平均单价
 */
typedef struct {
    currency_t currency;       /**< 计价货币代码 */
    quantity_t quantity;       /**< 当前持仓份额 */
    money_t    total_cost;     /**< 累计总成本基础 (含买入手续费) */
    money_t    total_cost_pnl; /**< 用于盈亏核算的净成本 (扣除分红/剔除手续费) */
    price_t    average_cost;   /**< 加权平均持仓单价 */
} mf_cost_basis_t;

/**
 * @brief 初始化空的成本基础
 */
mf_cost_basis_t mf_cost_basis_init(currency_t currency);

/**
 * @brief 应用买入建仓/加仓动作
 * @return 0 成功, -1 失败
 */
int mf_cost_basis_apply_buy(mf_cost_basis_t* cb,
                            quantity_t       qty,
                            money_t          amount,
                            money_t          fee);

/**
 * @brief 应用卖出平仓/减仓动作 (按持仓份额等比例缩减成本，计算平仓净盈亏)
 * @param[in,out] accum_realized_pnl 累计已实现盈亏指针
 * @return 0 成功, -1 失败 (如超卖)
 */
int mf_cost_basis_apply_sell(mf_cost_basis_t* cb,
                             money_t*         accum_realized_pnl,
                             quantity_t       qty,
                             money_t          amount,
                             money_t          fee);

/**
 * @brief 应用现金分红动作 (不改变持有份额，增加已实现收益，并冲减 total_cost_pnl)
 * @param[in,out] accum_realized_pnl 累计已实现盈亏指针
 * @return 0 成功, -1 失败
 */
int mf_cost_basis_apply_dividend(mf_cost_basis_t* cb,
                                 money_t*         accum_realized_pnl,
                                 money_t          dividend_amount);
