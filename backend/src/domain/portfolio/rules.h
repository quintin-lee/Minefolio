#pragma once

#include <stddef.h>
#include "domain/portfolio/entity.h"

/**
 * @brief 纯领域计算：按时间序重放交易事件流，核算各持仓项的已实现盈亏、当前市值与浮动盈亏
 */
int mf_portfolio_rule_apply_trade_events(mf_holding_item_t*                items,
                                         size_t                            item_count,
                                         const mf_portfolio_trade_event_t* events,
                                         size_t                            event_count);

/**
 * @brief 纯领域计算：汇总所有持仓项，产出投资组合全局 Summary
 */
int mf_portfolio_rule_aggregate_summary(const mf_holding_item_t* items,
                                        size_t                   item_count,
                                        currency_t               base_currency,
                                        mf_portfolio_summary_t*  out_summary);
