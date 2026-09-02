/**
 * @file market_controller.h
 * @brief 市场行情数据、汇率服务与定时同步配置控制器头文件
 *
 * 声明多源行情搜索（A股/港美股/基金/加密货币）、最新报价拉取、全量与单个资产市价同步、
 * 历史价格K线数据、多币种对CNY实时汇率维护以及行情数据源/代理设置相关的 HTTP 路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 注册市场行情与外汇服务模块相关的所有 HTTP 路由
 *
 * @details 注册包括标的搜索、报价查询、自动同步、历史价格、汇率查询/更新及代理设置等端点：
 *          - GET    /api/market/search: 标的代码/名称模糊搜索
 *          - GET    /api/market/quote: 单标的实时行情报价
 *          - POST   /api/market/sync: 一键同步更新当前用户全部持仓资产最新市价
 *          - POST   /api/market/sync/:asset_id: 同步单个资产市价
 *          - GET    /api/market/history/:asset_id: 获取资产历史行情走势数据
 *          - GET    /api/market/settings: 获取行情数据源、代理与定时同步配置
 *          - PUT    /api/market/settings: 更新行情配置
 *          - POST   /api/market/test-proxy: 测试行情服务连通性
 *          - GET    /api/market/exchange-rates (及 /api/market/fx-rates): 查询实时多币种汇率
 *          - POST   /api/market/exchange-rates (及 /api/market/fx-rates): 更新/手动维护币种汇率
 *          - GET    /api/market/fx-history: 获取汇率历史快照走势
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_market_routes(csilk_app_t* app);
