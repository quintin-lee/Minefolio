#pragma once

#include "csilk/csilk.h"
#include "application/portfolio/commands.h"
#include "application/portfolio/dtos.h"

/**
 * @brief 查询用户投资持仓报表及组合汇总（计算实时市值、浮动盈亏、浮动盈亏率与历史累计已实现盈亏）
 */
int portfolio_usecase_get_holdings(void*                        db_pool,
                                   const query_portfolio_cmd_t* cmd,
                                   csilk_json_t**               out_resp,
                                   portfolio_usecase_result_t*  out_res);

/**
 * @brief 查询用户交易表现与业绩报表 (Transaction Performance & PnL)
 */
int portfolio_usecase_get_performance(void*                        db_pool,
                                      const query_portfolio_cmd_t* cmd,
                                      csilk_json_t**               out_resp,
                                      portfolio_usecase_result_t*  out_res);

/**
 * @brief 查询用户仪表盘资产汇总 (Dashboard Net Worth, Category Breakdown, Recent Transactions)
 */
int portfolio_usecase_get_dashboard_summary(void*                        db_pool,
                                            const query_portfolio_cmd_t* cmd,
                                            csilk_json_t**               out_resp,
                                            portfolio_usecase_result_t*  out_res);
