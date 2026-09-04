#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "core/financial/currency.h"
#include "core/financial/money.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include "domain/portfolio/fx_table.h"
#include "domain/portfolio/portfolio.h"

/**
 * @brief 单项投资标的持仓实体 (Portfolio Holding Item)
 * @note 严格禁止依赖任何外部 DB、传输协议或 JSON 框架
 */
typedef struct mf_holding_item {
    int64_t    asset_id;
    char       name[128];
    char       asset_type[32]; /**< 标的类型: stock, fund, bond, crypto */
    currency_t currency;
    quantity_t quantity;       /**< 当前持仓份额 */
    price_t    net_value;      /**< 最新单位净值/市价 */
    money_t    cost_basis;     /**< 账面累计总成本 */
    money_t    market_value;   /**< 当前市值 = quantity * net_value */
    money_t    floating_pnl;   /**< 浮动盈亏 = market_value - cost_basis */
    double     floating_pct;   /**< 浮动盈亏率 (百分比) */
    money_t    realized_pnl;   /**< 历史累计已实现盈亏 */
} mf_holding_item_t;

/**
 * @brief 投资组合汇总实体 (Portfolio Summary)
 */
typedef struct mf_portfolio_summary {
    money_t total_market_value;
    money_t total_cost_basis;
    money_t total_floating_pnl;
    money_t total_realized_pnl;
    double  floating_pct;
} mf_portfolio_summary_t;

/**
 * @brief 用于持仓重放计算的时序交易事件事实 (Historical Trade Event Fact)
 */
typedef struct mf_portfolio_trade_event {
    int64_t    asset_id;
    char       type[32]; /**< buy, sell, income (分红派息) */
    quantity_t quantity;
    money_t    amount;
    price_t    price;
    money_t    fee;
} mf_portfolio_trade_event_t;
