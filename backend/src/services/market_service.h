#pragma once
#include "csilk/csilk.h"

/**
 * @file market_service.h
 * @brief 多源行情同步引擎（A股、美股、港股、公募基金、加密货币、外汇汇率）与自动调度服务
 */

/**
 * @brief 模糊搜索行情标的 (GET /api/market/search)
 * 智能跨源检索东财公募基金、腾讯 A 股/港股、Yahoo 美股/外汇/大宗商品
 * @param c HTTP 上下文（支持 query 检索词）
 */
void market_service_search(csilk_ctx_t* c);

/**
 * @brief 获取单个行情标的的实时行情最新快照 (GET /api/market/quote)
 * @param c HTTP 上下文（支持 symbol, source 查询参数）
 */
void market_service_quote(csilk_ctx_t* c);

/**
 * @brief 批量同步更新当前用户所有绑定了行情代码的资产净值/价格 (POST /api/market/sync-all)
 * @param c HTTP 上下文
 */
void market_service_sync_all(csilk_ctx_t* c);

/**
 * @brief 针对单个资产触发即时行情同步更新 (POST /api/assets/:id/sync-quote)
 * @param c HTTP 上下文
 */
void market_service_sync_single(csilk_ctx_t* c);

/**
 * @brief 查询指定资产的历史净值/价格 K 线走势 (GET /api/assets/:id/price-history)
 * @param c HTTP 上下文
 */
void market_service_price_history(csilk_ctx_t* c);

/**
 * @brief 获取行情同步全局配置（自动同步开关、同步间隔、交易时段模式、HTTP代理）(GET /api/market/settings)
 * @param c HTTP 上下文
 */
void market_service_get_settings(csilk_ctx_t* c);

/**
 * @brief 更新行情同步全局配置 (PUT /api/market/settings)
 * @param c HTTP 上下文
 */
void market_service_update_settings(csilk_ctx_t* c);

/**
 * @brief 测试行情网络与上游数据源连通性及延迟 (POST /api/market/test-proxy)
 * @param c HTTP 上下文
 */
void market_service_test_proxy(csilk_ctx_t* c);

/**
 * @brief 获取所有币种对基准币种的实时折算汇率字典 (GET /api/market/exchange-rates)
 * @param c HTTP 上下文
 */
void market_service_get_exchange_rates(csilk_ctx_t* c);

/**
 * @brief 手动设置或修改特定币种的汇率 (POST /api/market/exchange-rates)
 * @param c HTTP 上下文
 */
void market_service_update_exchange_rate(csilk_ctx_t* c);

/**
 * @brief 查询特定外币在最近 N 天内的每日汇率变动历史曲线 (GET /api/market/fx-history)
 * @param c HTTP 上下文
 */
void market_service_get_fx_history(csilk_ctx_t* c);

/**
 * @brief 执行单用户全量行情同步核心业务逻辑（供 HTTP 控制器与后台定时调度器复用）
 *
 * @param pool        数据库连接池
 * @param user_id     用户 ID
 * @param out_synced  输出参数：成功同步的资产计数（可为 NULL）
 * @param out_failed  输出参数：同步失败的资产计数（可为 NULL）
 * @return int        0 执行完成；-1 数据库连接失败
 */
int market_service_do_sync_user(csilk_db_pool_t* pool,
                                int64_t          user_id,
                                int*             out_synced,
                                int*             out_failed);
