#include "application/market/usecases.h"
#include "domain/market/entity.h"
#include "domain/market/repository.h"
#include "domain/market/rules.h"
#include "services/market/quote_engine.h"
#include "services/market/market_scheduler.h"
#include "services/market/exchange_rate_service.h"
#include "repositories/asset_repo.h"
#include "repositories/price_history_repo.h"
#include "common/balance.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void get_today_str(char* out, size_t cap) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(out, cap, "%Y-%m-%d", &tm_now);
}

int market_usecase_search(const search_market_cmd_t* cmd, csilk_json_t** out_list,
                          market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!cmd || !cmd->keyword || !cmd->keyword[0] || !out_list) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少搜索关键词 keyword");
        return -1;
    }

    market_search_item_t items[30];
    int n = quote_engine_search(cmd->keyword, items, 30);

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

    *out_list = arr;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int market_usecase_quote(const fetch_quote_cmd_t* cmd, csilk_json_t** out_quote,
                         market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!cmd || !cmd->symbol || !cmd->symbol[0] || !out_quote) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少标的代码 symbol");
        return -1;
    }

    market_quote_t q;
    if (quote_engine_fetch_quote(cmd->symbol, cmd->source, &q) != 0) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "获取行情失败或标的不存在");
        return -1;
    }

    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_string(obj, "symbol", q.symbol);
    csilk_json_add_string(obj, "name", q.name);
    csilk_json_add_string(obj, "source", q.source);
    csilk_json_add_number(obj, "current_price", q.current_price);
    csilk_json_add_number(obj, "change_percent", q.change_percent);
    csilk_json_add_string(obj, "currency", q.currency);
    csilk_json_add_string(obj, "quote_time", q.quote_time);

    *out_quote = obj;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int market_usecase_do_sync_user(void* pool, int64_t user_id, int* out_synced, int* out_failed) {
    csilk_json_t* assets = asset_list_for_sync((csilk_db_pool_t*)pool, user_id);
    if (!assets) {
        if (out_synced) *out_synced = 0;
        if (out_failed) *out_failed = 0;
        return 0;
    }

    int synced = 0;
    int failed = 0;
    char today[32];
    get_today_str(today, sizeof(today));

    size_t count = csilk_json_array_size(assets);
    for (size_t i = 0; i < count; i++) {
        const csilk_json_t* item = csilk_json_array_get(assets, i);
        int64_t aid = db_get_int(item, "id");
        int64_t uid = db_get_int(item, "user_id");
        const char* symbol = csilk_json_get_string(item, "symbol");
        const char* source = csilk_json_get_string(item, "quote_source");
        double old_nv = db_get_num(item, "net_value");
        double qty_val = db_get_num(item, "quantity");

        if (!symbol || !symbol[0]) continue;

        market_quote_t q;
        if (quote_engine_fetch_quote(symbol, source, &q) == 0 && q.current_price > 0) {
            double new_nv = q.current_price;
            currency_t cur = currency_from_str(q.currency ? q.currency : "CNY");
            price_t p_new;
            price_from_double(new_nv, 4, cur, &p_new);

            mf_market_repo_update_asset_quote(pool, uid, aid, p_new);
            mf_market_repo_record_price_history(pool, aid, today, p_new, cur);

            if (qty_val > 0 && old_nv > 0) {
                price_t p_old;
                price_from_double(old_nv, 4, cur, &p_old);
                quantity_t qty;
                quantity_from_double(qty_val, 4, &qty);
                money_t delta;
                mf_market_rule_calc_sync_delta(p_old, p_new, qty, cur, &delta);
                double delta_val = money_to_double(delta);
                if (delta_val != 0.0) {
                    balance_apply_delta((csilk_db_pool_t*)pool, aid, uid, delta_val,
                                        "asset_market_sync", aid, "Market quote sync");
                }
            }
            synced++;
        } else {
            failed++;
        }
    }

    csilk_json_free(assets);
    if (out_synced) *out_synced = synced;
    if (out_failed) *out_failed = failed;
    return 0;
}

int market_usecase_sync_all(void* pool, int64_t user_id, int* out_synced, int* out_failed,
                            market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    int synced = 0, failed = 0;
    market_usecase_do_sync_user(pool, user_id, &synced, &failed);

    if (out_synced) *out_synced = synced;
    if (out_failed) *out_failed = failed;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int market_usecase_sync_single(void* pool, int64_t user_id, int64_t asset_id,
                              csilk_json_t** out_res_data, market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || asset_id <= 0 || !out_res_data) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    csilk_json_t* asset_rows = asset_get((csilk_db_pool_t*)pool, user_id, asset_id);
    if (!asset_rows || csilk_json_array_size(asset_rows) == 0) {
        if (asset_rows) csilk_json_free(asset_rows);
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "资产不存在");
        return -1;
    }

    const csilk_json_t* item = csilk_json_array_get(asset_rows, 0);
    const char* symbol = csilk_json_get_string(item, "symbol");
    const char* source = csilk_json_get_string(item, "quote_source");
    double old_nv = db_get_num(item, "net_value");
    double qty_val = db_get_num(item, "quantity");

    if (!symbol || !symbol[0]) {
        csilk_json_free(asset_rows);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "该资产未绑定标的代码");
        return -1;
    }

    market_quote_t q;
    if (quote_engine_fetch_quote(symbol, source, &q) != 0 || q.current_price <= 0) {
        csilk_json_free(asset_rows);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "同步行情数据失败");
        return -1;
    }

    double new_nv = q.current_price;
    currency_t cur = currency_from_str(q.currency ? q.currency : "CNY");
    price_t p_new;
    price_from_double(new_nv, 4, cur, &p_new);

    mf_market_repo_update_asset_quote(pool, user_id, asset_id, p_new);

    char today[32];
    get_today_str(today, sizeof(today));
    mf_market_repo_record_price_history(pool, asset_id, today, p_new, cur);

    if (qty_val > 0 && old_nv > 0) {
        price_t p_old;
        price_from_double(old_nv, 4, cur, &p_old);
        quantity_t qty;
        quantity_from_double(qty_val, 4, &qty);
        money_t delta;
        mf_market_rule_calc_sync_delta(p_old, p_new, qty, cur, &delta);
        double delta_val = money_to_double(delta);
        if (delta_val != 0.0) {
            balance_apply_delta((csilk_db_pool_t*)pool, asset_id, user_id, delta_val,
                                "asset_market_sync", asset_id, "Market quote sync");
        }
    }

    csilk_json_free(asset_rows);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "symbol", q.symbol);
    csilk_json_add_string(res, "name", q.name);
    csilk_json_add_number(res, "net_value", new_nv);
    csilk_json_add_number(res, "change_percent", q.change_percent);
    csilk_json_add_string(res, "quote_time", q.quote_time);

    *out_res_data = res;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int market_usecase_price_history(void* pool, int64_t user_id, int64_t asset_id, int limit,
                                csilk_json_t** out_rows, market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || asset_id <= 0 || !out_rows) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    if (limit <= 0) limit = 90;
    csilk_json_t* rows = price_history_list_by_asset((csilk_db_pool_t*)pool, user_id, asset_id, limit);
    *out_rows = rows ? rows : csilk_json_array();
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int market_usecase_get_settings(csilk_json_t** out_settings, market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!out_settings) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    bool auto_sync = true;
    int interval_min = 30;
    char sync_mode[32] = "trading_hours";
    market_scheduler_get_config(&auto_sync, &interval_min, sync_mode, sizeof(sync_mode));

    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_string(obj, "market_proxy", quote_engine_get_proxy());
    csilk_json_add_bool(obj, "market_auto_sync", auto_sync);
    csilk_json_add_number(obj, "market_sync_interval_min", interval_min);
    csilk_json_add_string(obj, "market_sync_mode", sync_mode);

    *out_settings = obj;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int market_usecase_update_settings(const update_market_settings_cmd_t* cmd,
                                  market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!cmd) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    if (cmd->market_proxy) {
        quote_engine_set_proxy(cmd->market_proxy);
    }

    bool auto_sync = true;
    int interval_min = 30;
    char sync_mode[32] = "trading_hours";
    market_scheduler_get_config(&auto_sync, &interval_min, sync_mode, sizeof(sync_mode));

    if (cmd->has_auto_sync) {
        auto_sync = cmd->market_auto_sync;
    }
    if (cmd->has_interval && cmd->market_sync_interval_min >= 1) {
        interval_min = cmd->market_sync_interval_min;
    }
    if (cmd->market_sync_mode && cmd->market_sync_mode[0]) {
        strncpy(sync_mode, cmd->market_sync_mode, sizeof(sync_mode) - 1);
        sync_mode[sizeof(sync_mode) - 1] = '\0';
    }

    market_scheduler_set_config(auto_sync, interval_min, sync_mode);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int market_usecase_test_proxy(const char* proxy, csilk_json_t** out_data,
                             market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!out_data) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    char msg[256];
    int latency_ms = 0;
    int res = quote_engine_test_connection(proxy, msg, sizeof(msg), &latency_ms);

    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_bool(obj, "success", res == 0);
    csilk_json_add_string(obj, "message", msg);
    csilk_json_add_number(obj, "latency_ms", latency_ms);

    *out_data = obj;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int market_usecase_get_exchange_rates(csilk_json_t** out_rates, market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!out_rates) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    *out_rates = exchange_rate_list_all();
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int market_usecase_update_exchange_rate(void* pool, const update_exchange_rate_cmd_t* cmd,
                                       market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || !cmd->currency || !cmd->currency[0] || cmd->rate <= 0.0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "币种 (currency) 和汇率 (rate > 0) 不能为空");
        return -1;
    }

    exchange_rate_set(cmd->currency, cmd->rate);
    mf_market_repo_save_exchange_rate(pool, cmd->currency, cmd->rate);

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int market_usecase_get_fx_history(const char* currency, int days,
                                  csilk_json_t** out_history, market_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!out_history) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    if (days <= 0) days = 30;
    *out_history = exchange_rate_history_list(currency ? currency : "USD", days);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}
