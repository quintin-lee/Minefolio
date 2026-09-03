#include "interfaces/http/controllers/market_controller.h"
#include "application/market/usecases.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
api_market_search(csilk_ctx_t* c)
{
    search_market_cmd_t cmd = {
        .keyword = csilk_get_query(c, "keyword"),
    };

    csilk_json_t*           list = NULL;
    market_usecase_result_t res = {0};
    int                     rc = market_usecase_search(&cmd, &list, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, list);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
api_market_quote(csilk_ctx_t* c)
{
    fetch_quote_cmd_t cmd = {
        .symbol = csilk_get_query(c, "symbol"),
        .source = csilk_get_query(c, "source"),
    };

    csilk_json_t*           quote = NULL;
    market_usecase_result_t res = {0};
    int                     rc = market_usecase_quote(&cmd, &quote, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, quote);
    } else {
        respond_error(c, res.code ? res.code : 1003, res.message);
    }
}

void
api_market_sync_all(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    int                     synced = 0, failed = 0;
    market_usecase_result_t res = {0};
    int rc = market_usecase_sync_all(db_get_pool(), user_id, &synced, &failed, &res);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* out = csilk_json_object();
        csilk_json_add_number(out, "synced_count", synced);
        csilk_json_add_number(out, "failed_count", failed);
        respond_ok(c, out);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
api_market_sync_single(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    const char* id_str = csilk_get_param(c, "asset_id");
    if (!id_str) {
        respond_bad_request(c, "缺少 asset_id");
        return;
    }

    int64_t                 aid = atoll(id_str);
    csilk_json_t*           data = NULL;
    market_usecase_result_t res = {0};
    int rc = market_usecase_sync_single(db_get_pool(), user_id, aid, &data, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, data);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_market_price_history(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    const char* id_str = csilk_get_param(c, "asset_id");
    if (!id_str) {
        respond_bad_request(c, "缺少 asset_id");
        return;
    }

    const char* lim_str = csilk_get_query(c, "limit");
    int         limit = lim_str ? atoi(lim_str) : 90;

    int64_t                 aid = atoll(id_str);
    csilk_json_t*           rows = NULL;
    market_usecase_result_t res = {0};
    market_usecase_price_history(db_get_pool(), user_id, aid, limit, &rows, &res);

    respond_ok(c, rows ? rows : csilk_json_array());
}

void
api_market_get_settings(csilk_ctx_t* c)
{
    csilk_json_t*           obj = NULL;
    market_usecase_result_t res = {0};
    market_usecase_get_settings(&obj, &res);
    respond_ok(c, obj);
}

void
api_market_update_settings(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "无效的 JSON 数据");
        return;
    }

    update_market_settings_cmd_t cmd = {
        .market_proxy = csilk_json_get_string(body, "market_proxy"),
        .has_auto_sync = csilk_json_get(body, "market_auto_sync") != NULL,
        .market_auto_sync = csilk_json_get_bool(body, "market_auto_sync"),
        .has_interval = csilk_json_get(body, "market_sync_interval_min") != NULL,
        .market_sync_interval_min = (int)db_get_int(body, "market_sync_interval_min"),
        .market_sync_mode = csilk_json_get_string(body, "market_sync_mode"),
    };

    market_usecase_result_t res = {0};
    market_usecase_update_settings(&cmd, &res);
    csilk_json_free(body);
    respond_ok_null(c);
}

void
api_market_test_proxy(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    const char*   proxy = body ? csilk_json_get_string(body, "market_proxy") : NULL;

    csilk_json_t*           data = NULL;
    market_usecase_result_t res = {0};
    market_usecase_test_proxy(proxy, &data, &res);

    if (body) {
        csilk_json_free(body);
    }
    respond_ok(c, data);
}

void
api_market_get_exchange_rates(csilk_ctx_t* c)
{
    csilk_json_t*           rates = NULL;
    market_usecase_result_t res = {0};
    market_usecase_get_exchange_rates(&rates, &res);
    respond_ok(c, rates);
}

void
api_market_update_exchange_rate(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "无效的 JSON 数据");
        return;
    }

    update_exchange_rate_cmd_t cmd = {
        .currency = csilk_json_get_string(body, "currency"),
        .rate = db_get_num(body, "rate"),
    };

    market_usecase_result_t res = {0};
    int                     rc = market_usecase_update_exchange_rate(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
api_market_get_fx_history(csilk_ctx_t* c)
{
    const char* currency = csilk_get_query(c, "currency");
    const char* days_str = csilk_get_query(c, "days");
    int         days = (days_str && days_str[0]) ? atoi(days_str) : 30;

    csilk_json_t*           list = NULL;
    market_usecase_result_t res = {0};
    market_usecase_get_fx_history(currency, days, &list, &res);
    respond_ok(c, list);
}

void
register_market_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/market/search",
                      api_market_search,
                      NULL,
                      NULL,
                      "Search market symbols",
                      "Search stocks, funds, and crypto");
    csilk_app_get_ext(app,
                      "/api/market/quote",
                      api_market_quote,
                      NULL,
                      NULL,
                      "Get market quote",
                      "Fetch single market quote");
    csilk_app_post_ext(app,
                       "/api/market/sync",
                       api_market_sync_all,
                       NULL,
                       NULL,
                       "Sync all asset quotes",
                       "Sync all market quotes for current user");
    csilk_app_post_ext(app,
                       "/api/market/sync/:asset_id",
                       api_market_sync_single,
                       NULL,
                       NULL,
                       "Sync single asset quote",
                       "Sync market quote for a single asset");
    csilk_app_get_ext(app,
                      "/api/market/history/:asset_id",
                      api_market_price_history,
                      NULL,
                      NULL,
                      "Get price history",
                      "Fetch historical price data for asset");
    csilk_app_get_ext(app,
                      "/api/market/settings",
                      api_market_get_settings,
                      NULL,
                      NULL,
                      "Get market settings",
                      "Get market proxy and sync settings");
    csilk_app_put_ext(app,
                      "/api/market/settings",
                      api_market_update_settings,
                      NULL,
                      NULL,
                      "Update market settings",
                      "Update market proxy and sync settings");
    csilk_app_post_ext(app,
                       "/api/market/test-proxy",
                       api_market_test_proxy,
                       NULL,
                       NULL,
                       "Test market connection",
                       "Test connectivity to market data providers");
    csilk_app_get_ext(app,
                      "/api/market/exchange-rates",
                      api_market_get_exchange_rates,
                      NULL,
                      NULL,
                      "Get exchange rates",
                      "Get real-time multi-currency exchange rates to CNY");
    csilk_app_post_ext(app,
                       "/api/market/exchange-rates",
                       api_market_update_exchange_rate,
                       NULL,
                       NULL,
                       "Update exchange rate",
                       "Update currency exchange rate to CNY");
    csilk_app_get_ext(app,
                      "/api/market/fx-rates",
                      api_market_get_exchange_rates,
                      NULL,
                      NULL,
                      "Get FX rates",
                      "Get FX rates alias");
    csilk_app_post_ext(app,
                       "/api/market/fx-rates",
                       api_market_update_exchange_rate,
                       NULL,
                       NULL,
                       "Update FX rate",
                       "Update FX rate alias");
    csilk_app_get_ext(app,
                      "/api/market/fx-history",
                      api_market_get_fx_history,
                      NULL,
                      NULL,
                      "Get FX historical trend",
                      "Fetch historical exchange rate snapshots");
}
