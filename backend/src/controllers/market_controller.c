#include "controllers/market_controller.h"
#include "services/market_service.h"

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
