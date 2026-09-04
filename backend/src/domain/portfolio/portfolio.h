#pragma once

#include "core/financial/currency.h"
#include "core/financial/money.h"
#include "domain/asset/position.h"
#include "domain/portfolio/fx_table.h"
#include <stddef.h>

/**
 * @brief 组合单项资产持仓分析投影 (Portfolio Position Allocation Item)
 */
typedef struct {
    int64_t    asset_id;
    currency_t native_currency;
    money_t    native_market_value;
    money_t    converted_market_value;   /**< 折算为 reporting_currency 的市值 */
    money_t    converted_cost_basis;     /**< 折算为 reporting_currency 的成本 */
    money_t    converted_unrealized_pnl; /**< 折算为 reporting_currency 的浮动盈亏 */
    money_t    converted_realized_pnl;   /**< 折算为 reporting_currency 的已实现盈亏 */
    double     weight;                   /**< 占组合总市值权重比例 (0.0 ~ 1.0) */
} mf_portfolio_item_t;

/**
 * @brief 投资组合纯领域聚合根 (Portfolio Aggregate Root)
 * 职责：负责 aggregation, allocation, valuation, performance, risk metrics
 * 绝不负责交易写入！
 */
typedef struct {
    currency_t           reporting_currency;
    size_t               item_count;
    mf_portfolio_item_t* items;

    money_t total_market_value;    /**< 组合总市值 (reporting_currency) */
    money_t total_cost_basis;      /**< 组合总成本基础 (reporting_currency) */
    money_t total_realized_pnl;    /**< 组合累计已实现盈亏 (reporting_currency) */
    money_t total_unrealized_pnl;  /**< 组合当前浮动盈亏 (reporting_currency) */
    money_t total_pnl;             /**< 综合总盈亏 (reporting_currency) */
    double  total_return_pct;      /**< 综合总回报率 (%) */
    double  unrealized_pct;        /**< 浮动盈亏率 (%) */

    /* 风险度量与集中度指标 (Risk Metrics & Concentration) */
    double  max_holding_weight;    /**< 最大单一标的持仓权重比例 (0.0 ~ 1.0) */
    int64_t max_holding_asset_id;  /**< 第一大重仓标的 ID */
    double  herfindahl_index;      /**< 赫芬达尔集中度指数 HHI = sum(weight_i^2) (0.0 ~ 1.0) */
} mf_portfolio_t;

/**
 * @brief 纯领域计算：汇总所有持仓，显式进行多币种折算，计算资产配置权重与风险集中度
 * @param positions 持仓列表
 * @param pos_count 持仓项数
 * @param reporting_currency 报告基准币种
 * @param fx_table 显式汇率表
 * @param[out] out_portfolio 聚合结果输出结构
 * @return 0 成功, -1 失败 (例如缺少必要汇率导致无法折算，杜绝隐式默认 1:1)
 */
int mf_portfolio_aggregate(const mf_position_t*      positions,
                           size_t                    pos_count,
                           currency_t                reporting_currency,
                           const mf_fx_rate_table_t* fx_table,
                           mf_portfolio_t*           out_portfolio);

/**
 * @brief 释放投资组合内部动态资源
 */
void mf_portfolio_free(mf_portfolio_t* portfolio);
