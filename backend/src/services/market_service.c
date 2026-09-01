#include "services/market_service.h"
#include "services/market/quote_engine.h"
#include "services/market/market_scheduler.h"
#include "services/market/exchange_rate_service.h"
#include "repositories/asset_repo.h"
#include "repositories/price_history_repo.h"
#include "common/balance.h"
#include "common/ctx.h"
#include "common/response.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
get_today_str(char* out, size_t cap)
{
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(out, cap, "%Y-%m-%d", &tm_now);
}

int
market_service_do_sync_user(csilk_db_pool_t* pool,
                            int64_t          user_id,
                            int*             out_synced,
                            int*             out_failed)
{
    csilk_json_t* assets = asset_list_for_sync(pool, user_id);
    if (!assets) {
        if (out_synced) {
            *out_synced = 0;
        }
        if (out_failed) {
            *out_failed = 0;
        }
        return 0;
    }

    int  synced = 0;
    int  failed = 0;
    char today[32];
    get_today_str(today, sizeof(today));

    size_t count = csilk_json_array_size(assets);
    for (size_t i = 0; i < count; i++) {
        const csilk_json_t* item = csilk_json_array_get(assets, i);
        int64_t             aid = db_get_int(item, "id");
        int64_t             uid = db_get_int(item, "user_id");
        const char*         symbol = csilk_json_get_string(item, "symbol");
        const char*         source = csilk_json_get_string(item, "quote_source");
        double              old_nv = db_get_num(item, "net_value");
        double              qty = db_get_num(item, "quantity");

        if (!symbol || !symbol[0]) {
            continue;
        }

        market_quote_t q;
        if (quote_engine_fetch_quote(symbol, source, &q) == 0 && q.current_price > 0) {
            double new_nv = q.current_price;
            asset_update_market_quote(pool, uid, aid, new_nv);
            price_history_record(pool, aid, today, new_nv, q.currency);

            if (qty > 0 && old_nv > 0) {
                double delta = (new_nv - old_nv) * qty;
                if (delta != 0) {
                    balance_apply_delta(
                        pool, aid, uid, delta, "asset_market_sync", aid, "Market quote sync");
                }
            }
            synced++;
        } else {
            failed++;
        }
    }

    csilk_json_free(assets);
    if (out_synced) {
        *out_synced = synced;
    }
    if (out_failed) {
        *out_failed = failed;
    }
    return 0;
}

void
market_service_search(csilk_ctx_t* c)
{
    const char* kw = csilk_get_query(c, "keyword");
    if (!kw || !kw[0]) {
        respond_bad_request(c, "缺少搜索关键词 keyword");
        return;
    }

    market_search_item_t items[30];
    int                  n = quote_engine_search(kw, items, 30);

    csilk_json_t* arr = csilk_json_array();
    for (int i = 0; i < n; i++) {
        csilk_json_t* obj = csilk_json_object();
        csilk_json_add_string(obj, "symbol", items[i].symbol);
        csilk_json_add_string(obj, "name", items[i].name);
        csilk_json_add_string(obj, "source", items[i].source);
        csilk_json_add_string(obj, "market_desc", items[i].market_desc);
        csilk_json_add_number(obj, "current_price", items[i].current_price);
        csilk_json_add_string(obj, "currency", items[i].currency);
        csilk_json_array_append(arr, obj);
    }

    respond_ok(c, arr);
}

void
market_service_quote(csilk_ctx_t* c)
{
    const char* symbol = csilk_get_query(c, "symbol");
    const char* source = csilk_get_query(c, "source");
    if (!symbol || !symbol[0]) {
        respond_bad_request(c, "缺少标的代码 symbol");
        return;
    }

    market_quote_t q;
    if (quote_engine_fetch_quote(symbol, source, &q) != 0) {
        respond_error(c, 1003, "获取行情失败或标的不存在");
        return;
    }

    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_string(obj, "symbol", q.symbol);
    csilk_json_add_string(obj, "name", q.name);
    csilk_json_add_string(obj, "source", q.source);
    csilk_json_add_number(obj, "current_price", q.current_price);
    csilk_json_add_number(obj, "change_percent", q.change_percent);
    csilk_json_add_string(obj, "currency", q.currency);
    csilk_json_add_string(obj, "quote_time", q.quote_time);

    respond_ok(c, obj);
}

void
market_service_sync_all(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    int synced = 0, failed = 0;
    market_service_do_sync_user(db_get_pool(), user_id, &synced, &failed);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "synced_count", synced);
    csilk_json_add_number(res, "failed_count", failed);
    respond_ok(c, res);
}

void
market_service_sync_single(csilk_ctx_t* c)
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

    int64_t          aid = atoll(id_str);
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    asset_rows = asset_get(pool, user_id, aid);
    if (!asset_rows || csilk_json_array_size(asset_rows) == 0) {
        if (asset_rows) {
            csilk_json_free(asset_rows);
        }
        respond_not_found(c);
        return;
    }

    const csilk_json_t* item = csilk_json_array_get(asset_rows, 0);
    const char*         symbol = csilk_json_get_string(item, "symbol");
    const char*         source = csilk_json_get_string(item, "quote_source");
    double              old_nv = db_get_num(item, "net_value");
    double              qty = db_get_num(item, "quantity");

    if (!symbol || !symbol[0]) {
        csilk_json_free(asset_rows);
        respond_bad_request(c, "该资产未绑定标的代码");
        return;
    }

    market_quote_t q;
    if (quote_engine_fetch_quote(symbol, source, &q) != 0 || q.current_price <= 0) {
        csilk_json_free(asset_rows);
        respond_error(c, 500, "同步行情数据失败");
        return;
    }

    double new_nv = q.current_price;
    asset_update_market_quote(pool, user_id, aid, new_nv);

    char today[32];
    get_today_str(today, sizeof(today));
    price_history_record(pool, aid, today, new_nv, q.currency);

    if (qty > 0 && old_nv > 0) {
        double delta = (new_nv - old_nv) * qty;
        if (delta != 0) {
            balance_apply_delta(
                pool, aid, user_id, delta, "asset_market_sync", aid, "Market quote sync");
        }
    }

    csilk_json_free(asset_rows);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "symbol", q.symbol);
    csilk_json_add_string(res, "name", q.name);
    csilk_json_add_number(res, "net_value", new_nv);
    csilk_json_add_number(res, "change_percent", q.change_percent);
    csilk_json_add_string(res, "quote_time", q.quote_time);
    respond_ok(c, res);
}

void
market_service_price_history(csilk_ctx_t* c)
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
    if (limit <= 0) {
        limit = 90;
    }

    int64_t       aid = atoll(id_str);
    csilk_json_t* rows = price_history_list_by_asset(db_get_pool(), user_id, aid, limit);
    if (!rows) {
        rows = csilk_json_array();
    }

    respond_ok(c, rows);
}

void
market_service_get_settings(csilk_ctx_t* c)
{
    bool auto_sync = true;
    int  interval_min = 30;
    char sync_mode[32] = "trading_hours";
    market_scheduler_get_config(&auto_sync, &interval_min, sync_mode, sizeof(sync_mode));

    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_string(obj, "market_proxy", quote_engine_get_proxy());
    csilk_json_add_bool(obj, "market_auto_sync", auto_sync);
    csilk_json_add_number(obj, "market_sync_interval_min", interval_min);
    csilk_json_add_string(obj, "market_sync_mode", sync_mode);
    respond_ok(c, obj);
}

void
market_service_update_settings(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "无效的 JSON 数据");
        return;
    }

    const char* proxy = csilk_json_get_string(body, "market_proxy");
    quote_engine_set_proxy(proxy);

    bool auto_sync = true;
    int  interval_min = 30;
    char sync_mode[32] = "trading_hours";
    market_scheduler_get_config(&auto_sync, &interval_min, sync_mode, sizeof(sync_mode));

    if (csilk_json_get(body, "market_auto_sync")) {
        auto_sync = csilk_json_get_bool(body, "market_auto_sync");
    }
    if (csilk_json_get(body, "market_sync_interval_min")) {
        interval_min = (int)db_get_int(body, "market_sync_interval_min");
    }
    const char* mode = csilk_json_get_string(body, "market_sync_mode");
    if (mode && mode[0]) {
        strncpy(sync_mode, mode, sizeof(sync_mode) - 1);
        sync_mode[sizeof(sync_mode) - 1] = '\0';
    }

    market_scheduler_set_config(auto_sync, interval_min, sync_mode);

    csilk_json_free(body);
    respond_ok_null(c);
}

void
market_service_test_proxy(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    const char*   proxy = NULL;
    if (body) {
        proxy = csilk_json_get_string(body, "market_proxy");
    }

    char msg[256];
    int  latency_ms = 0;
    int  res = quote_engine_test_connection(proxy, msg, sizeof(msg), &latency_ms);

    if (body) {
        csilk_json_free(body);
    }

    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_bool(obj, "success", res == 0);
    csilk_json_add_string(obj, "message", msg);
    csilk_json_add_number(obj, "latency_ms", latency_ms);
    respond_ok(c, obj);
}

void
market_service_get_exchange_rates(csilk_ctx_t* c)
{
    csilk_json_t* rates = exchange_rate_list_all();
    respond_ok(c, rates);
}
