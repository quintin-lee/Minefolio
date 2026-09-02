/**
 * @file report_controller.h
 * @brief 统计报表、持仓分析与仪表盘数据控制器头文件
 *
 * 声明涵盖收支月报/年报/趋势、分类与标签统计、资产配置与净值走势、投资持仓盈亏（PnL）、
 * 多币种汇率折算与外汇损益（FX PnL）以及首页 Dashboard 综合总览相关的 HTTP 端点和路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 获取月度收支明细分类报表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/expense/monthly
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - month: 目标月份 (string, 可选, 格式 "YYYY-MM", 默认当前月份)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"month": "2026-09", "total_expense": 4500.0, "total_income": 12000.0, "categories": [...]}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_expense_monthly(csilk_ctx_t* c);

/**
 * @brief 获取近 N 个月收支变动趋势
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/expense/trend
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - months: 回溯月数 (int, 可选, 默认 6, 如 6 或 12)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"month": "2026-04", "expense": 3200.0, "income": 10000.0}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_expense_trend(csilk_ctx_t* c);

/**
 * @brief 获取年度收支汇总报表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/expense/yearly
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - year: 目标年份 (int, 可选, 默认当前年份)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"year": 2026, "total_expense": 50000.0, "total_income": 150000.0, "months": [...]}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_expense_yearly(csilk_ctx_t* c);

/**
 * @brief 获取按分类聚合的支出分布统计
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/expense/category
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - start_date: 起始日期 (string, 可选, "YYYY-MM-DD")
 *          - end_date: 截止日期 (string, 可选, "YYYY-MM-DD")
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"category_id": 1, "category_name": "餐饮美食", "total": 1200.0, "percentage": 30.5}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_expense_category(csilk_ctx_t* c);

/**
 * @brief 获取按标签聚合的支出统计
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/expense/tag
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - start_date: 起始日期 (string, 可选)
 *          - end_date: 截止日期 (string, 可选)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"tag_id": 1, "tag_name": "外卖", "total": 500.0}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_expense_tag(csilk_ctx_t* c);

/**
 * @brief 获取历史资产与净资产走势趋势
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/asset/trend
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - months: 月数 (int, 可选, 默认 12)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"date": "2026-08", "total_assets": 150000.0, "net_worth": 120000.0}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_asset_trend(csilk_ctx_t* c);

/**
 * @brief 获取资产配置大类占比分布
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/asset/breakdown
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"category_name": "投资资产", "amount": 80000.0, "ratio": 0.65}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_asset_breakdown(csilk_ctx_t* c);

/**
 * @brief 获取投资交易历史表现与综合 PnL 盈亏报表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/transaction/performance
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - asset_id: 资产 ID (int64, 可选, 查看单只标的投资表现)
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"realized_pnl": 1500.0, "unrealized_pnl": 3200.0, "total_return_rate": 0.18, "items": [...]}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_transaction_performance(csilk_ctx_t* c);

/**
 * @brief 获取当前证券/基金/加密货币投资持仓明细报表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/holdings
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"asset_id": 1, "name": "贵州茅台", "quantity": 100, "avg_cost": 1500.0, "current_price": 1700.0, "floating_pnl": 20000.0}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_holdings(csilk_ctx_t* c);

/**
 * @brief 获取资产汇总概览（总资产、总负债与净资产）
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/asset/summary
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"total_assets": 200000.0, "total_liabilities": 50000.0, "net_worth": 150000.0}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_asset_summary(csilk_ctx_t* c);

/**
 * @brief 获取多币种资产分布与折算净资产汇总
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/multi-currency-summary (别名: /api/reports/currency-summary)
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - base_currency: 目标折算本位币 (string, 可选, 默认 "CNY")
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"base_currency": "CNY", "total_net_worth_cny": 180000.0, "currencies": [{"currency": "USD", "raw_amount": 5000.0, "rate_to_cny": 7.25, "converted_cny": 36250.0}, ...]}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_multi_currency_summary(csilk_ctx_t* c);

/**
 * @brief 获取外币资产汇率波动损益与标的收益归因报表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/reports/fx-pnl
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"asset_id": 2, "name": "Apple Inc", "asset_pnl_usd": 300.0, "fx_gain_loss_cny": 120.0, "total_pnl_cny": 2295.0}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void report_fx_pnl(csilk_ctx_t* c);

/**
 * @brief 获取首页 Dashboard 综合总览数据
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/summary
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          查询参数 (Query Parameters):
 *          - ledger_id: 账本 ID (int64, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"net_worth": 150000.0, "month_expense": 3200.0, "month_income": 8000.0, "recent_transactions": [...]}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void summary_get(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册统计报表与数据看板相关的所有 HTTP 路由
 *
 * @details 注册包括收支分析、资产走势、投资持仓、外汇损益及首页 Dashboard 等所有报表端点。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_report_routes(csilk_app_t* app);
