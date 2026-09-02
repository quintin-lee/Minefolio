/**
 * @file market_controller.c
 * @brief 市场行情数据、汇率服务与定时同步配置控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器负责将 HTTP 请求路由映射并分发至
 * services/market_service.c 中实现的多源行情、汇率换算与定时拉取调度服务。
 */

#include "controllers/market_controller.h"
#include "services/market_service.h"

/**
 * @brief 注册市场行情与外汇服务模块的所有 HTTP 路由
 *
 * @details 详细端点定义与参数说明：
 *
 * 1. GET /api/market/search
 *    - 功能: 搜索股票、基金、加密货币标的（支持东财、腾讯、Yahoo Finance、Binance 等）
 *    - 鉴权: JWT (Bearer Token)
 *    - 查询参数: q (string, 标的代码或关键字, 如 "600519", "AAPL", "BTC")
 *    - 响应: 200 OK {"code": 0, "data": [{"symbol": "600519", "name": "贵州茅台", "market": "SH", "type": "stock", "currency": "CNY"}, ...]}
 *
 * 2. GET /api/market/quote
 *    - 功能: 获取单个标的的实时最新价格报价
 *    - 鉴权: JWT (Bearer Token)
 *    - 查询参数: symbol (string), market (string, 可选)
 *    - 响应: 200 OK {"code": 0, "data": {"symbol": "600519", "price": 1720.50, "change_pct": 1.25, "currency": "CNY", "timestamp": 1725260000}}
 *
 * 3. POST /api/market/sync
 *    - 功能: 一键拉取最新报价并更新当前用户所有持仓投资资产的 net_value 及市值
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": {"synced": 8, "failed": 0}}
 *
 * 4. POST /api/market/sync/:asset_id
 *    - 功能: 单独同步指定资产的最新市场单价
 *    - 鉴权: JWT (Bearer Token)
 *    - 路径参数: asset_id (int64)
 *    - 响应: 200 OK {"code": 0, "data": {"price": 1720.50, "updated": true}}
 *
 * 5. GET /api/market/history/:asset_id
 *    - 功能: 查询资产历史价格数据（K线/收盘走势）
 *    - 鉴权: JWT (Bearer Token)
 *    - 路径参数: asset_id (int64)
 *    - 查询参数: range (string, "1m"|"3m"|"1y"|"all", 默认 "1m")
 *    - 响应: 200 OK {"code": 0, "data": [{"date": "2026-08-01", "price": 1680.0}, ...]}
 *
 * 6. GET /api/market/settings
 *    - 功能: 获取行情数据源配置、HTTP 代理及定时同步开关
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": {"proxy_url": "http://127.0.0.1:7890", "auto_sync_enabled": true, "sync_interval_mins": 30, ...}}
 *
 * 7. PUT /api/market/settings
 *    - 功能: 更新行情数据源配置与代理
 *    - 鉴权: JWT (Bearer Token)
 *    - 请求体: {"proxy_url": "http://127.0.0.1:7890", "auto_sync_enabled": true}
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 8. POST /api/market/test-proxy
 *    - 功能: 测试行情数据源及代理服务器的网络连通性
 *    - 鉴权: JWT (Bearer Token)
 *    - 请求体: {"proxy_url": "http://127.0.0.1:7890"}
 *    - 响应: 200 OK {"code": 0, "data": {"success": true, "latency_ms": 320}}
 *
 * 9. GET /api/market/exchange-rates (别名: GET /api/market/fx-rates)
 *    - 功能: 获取各主要外币（USD, HKD, EUR, JPY, GBP 等）对本位币 CNY 的实时汇率
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": {"USD": 7.25, "HKD": 0.93, "EUR": 7.85, ...}}
 *
 * 10. POST /api/market/exchange-rates (别名: POST /api/market/fx-rates)
 *     - 功能: 手动更新或录入外币对 CNY 汇率
 *     - 鉴权: JWT (Bearer Token)
 *     - 请求体: {"currency": "USD", "rate_to_cny": 7.25}
 *     - 响应: 200 OK {"code": 0, "data": null}
 *
 * 11. GET /api/market/fx-history
 *     - 功能: 获取各外币汇率历史变动快照趋势
 *     - 鉴权: JWT (Bearer Token)
 *     - 查询参数: currency (string, 如 "USD"), days (int, 可选, 默认 30)
 *     - 响应: 200 OK {"code": 0, "data": [{"date": "2026-08-01", "rate": 7.21}, ...]}
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void
register_market_routes(csilk_app_t* app)

{
    csilk_app_get_ext(app,
                      "/api/market/search",
                      market_service_search,
                      NULL,
                      NULL,
                      "Search market symbols",
                      "Search stocks, funds, and crypto");

    csilk_app_get_ext(app,
                      "/api/market/quote",
                      market_service_quote,
                      NULL,
                      NULL,
                      "Get market quote",
                      "Fetch single market quote");

    csilk_app_post_ext(app,
                       "/api/market/sync",
                       market_service_sync_all,
                       NULL,
                       NULL,
                       "Sync all asset quotes",
                       "Sync all market quotes for current user");

    csilk_app_post_ext(app,
                       "/api/market/sync/:asset_id",
                       market_service_sync_single,
                       NULL,
                       NULL,
                       "Sync single asset quote",
                       "Sync market quote for a single asset");

    csilk_app_get_ext(app,
                      "/api/market/history/:asset_id",
                      market_service_price_history,
                      NULL,
                      NULL,
                      "Get price history",
                      "Fetch historical price data for asset");

    csilk_app_get_ext(app,
                      "/api/market/settings",
                      market_service_get_settings,
                      NULL,
                      NULL,
                      "Get market settings",
                      "Get market proxy and sync settings");

    csilk_app_put_ext(app,
                      "/api/market/settings",
                      market_service_update_settings,
                      NULL,
                      NULL,
                      "Update market settings",
                      "Update market proxy and sync settings");

    csilk_app_post_ext(app,
                       "/api/market/test-proxy",
                       market_service_test_proxy,
                       NULL,
                       NULL,
                       "Test market connection",
                       "Test connectivity to market data providers");

    csilk_app_get_ext(app,
                      "/api/market/exchange-rates",
                      market_service_get_exchange_rates,
                      NULL,
                      NULL,
                      "Get exchange rates",
                      "Get real-time multi-currency exchange rates to CNY");

    csilk_app_post_ext(app,
                       "/api/market/exchange-rates",
                       market_service_update_exchange_rate,
                       NULL,
                       NULL,
                       "Update exchange rate",
                       "Update currency exchange rate to CNY");

    csilk_app_get_ext(app,
                      "/api/market/fx-rates",
                      market_service_get_exchange_rates,
                      NULL,
                      NULL,
                      "Get FX rates",
                      "Get FX rates alias");

    csilk_app_post_ext(app,
                       "/api/market/fx-rates",
                       market_service_update_exchange_rate,
                       NULL,
                       NULL,
                       "Update FX rate",
                       "Update FX rate alias");

    csilk_app_get_ext(app,
                      "/api/market/fx-history",
                      market_service_get_fx_history,
                      NULL,
                      NULL,
                      "Get FX historical trend",
                      "Fetch historical exchange rate snapshots");
}
