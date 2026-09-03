#pragma once

#include <stdint.h>
#include <stddef.h>
#include "domain/portfolio/entity.h"

/**
 * @brief 投资组合仓储抽象契约接口 (Domain Portfolio Repository Contract)
 * @note 纯 C 契约，入参出参仅传递领域实体与标量，严禁返回 JSON 节点或直接编写 SQL
 */

/**
 * @brief 查询用户的所有投资标的持仓基线事实 (股票、基金、债券、加密资产)
 * @return 0: 成功, -1: 失败
 */
int mf_portfolio_repo_get_holdings(void*               db_pool,
                                   int64_t             user_id,
                                   mf_holding_item_t** out_items,
                                   size_t*             out_count);

/**
 * @brief 释放持仓实体数组内存
 */
void mf_portfolio_repo_free_holdings(mf_holding_item_t* items, size_t count);

/**
 * @brief 查询用户用于持仓盈亏重放的时序交易事实列表 (按交易时间升序)
 * @return 0: 成功, -1: 失败
 */
int mf_portfolio_repo_get_trade_events(void*                        db_pool,
                                       int64_t                      user_id,
                                       mf_portfolio_trade_event_t** out_events,
                                       size_t*                      out_count);

/**
 * @brief 释放交易事件事实列表内存
 */
void mf_portfolio_repo_free_trade_events(mf_portfolio_trade_event_t* events, size_t count);
