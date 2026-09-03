#pragma once

#include "csilk/csilk.h"

/**
 * @brief 投资持仓报表控制器处理器
 */
void api_portfolio_holdings(csilk_ctx_t* c);

/**
 * @brief 交易表现及盈亏控制器处理器
 */
void api_portfolio_performance(csilk_ctx_t* c);

/**
 * @brief 仪表盘资产总览控制器处理器
 */
void api_portfolio_dashboard_summary(csilk_ctx_t* c);

/**
 * @brief 注册投资组合相关路由
 */
void register_portfolio_routes(csilk_app_t* app);
