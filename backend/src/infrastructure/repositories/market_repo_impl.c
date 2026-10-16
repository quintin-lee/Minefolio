#include "infrastructure/repositories/market_repo_impl.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
mf_market_repo_save_exchange_rate(void* pool, const char* currency, double rate)
{
    if (!pool || !currency || !currency[0] || rate <= 0.0) {
        return -1;
    }
    char rate_str[64];
    snprintf(rate_str, sizeof(rate_str), "%.6f", rate);

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "INSERT INTO exchange_rates (base_currency, target_currency, rate, updated_at) "
        "VALUES (?, 'CNY', ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(base_currency, target_currency) DO UPDATE SET rate = excluded.rate, "
        "updated_at = CURRENT_TIMESTAMP",
        (const char*[]){currency, rate_str, NULL});
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

int
mf_market_repo_record_price_history(
    void* pool, int64_t asset_id, const char* date, price_t price, currency_t cur)
{
    if (!pool || asset_id <= 0 || !date || !date[0]) {
        return -1;
    }
    char aid_str[32], price_str[64];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(price_str, sizeof(price_str), "%.4f", price_to_double(price));
    const char* currency = currency_code(&cur);

    const char* sql = "INSERT INTO asset_price_history (asset_id, price_date, price, currency) "
                      "VALUES (?, ?, ?, ?) "
                      "ON CONFLICT(asset_id, price_date) DO UPDATE SET price=EXCLUDED.price, "
                      "currency=EXCLUDED.currency";
    return csilk_db_exec_param(
        (csilk_db_pool_t*)pool, sql, (const char*[]){aid_str, date, price_str, currency, NULL});
}

int
mf_market_repo_update_asset_quote(void* pool, int64_t user_id, int64_t asset_id, price_t price)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return -1;
    }
    char uid_str[32], aid_str[32], price_str[64];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(price_str, sizeof(price_str), "%.4f", price_to_double(price));

    csilk_json_t* res =
        csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                  "UPDATE assets SET net_value=?, updated_at=CURRENT_TIMESTAMP "
                                  "WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){price_str, aid_str, uid_str, NULL});
    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : -1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

csilk_json_t*
mf_market_repo_price_history_list(void* pool, int64_t user_id, int64_t asset_id, int limit)
{
    char uid_str[32], aid_str[32], lim_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(lim_str, sizeof(lim_str), "%d", limit > 0 ? limit : 90);

    const char* sql = "SELECT h.id, h.asset_id, "
                      "CAST(h.price_date AS TEXT) as price_date, "
                      "COALESCE(CAST(h.price AS REAL), 0.0) as price, "
                      "h.currency, "
                      "CAST(h.created_at AS TEXT) as created_at "
                      "FROM asset_price_history h "
                      "JOIN assets a ON h.asset_id = a.id "
                      "WHERE a.user_id = ? AND h.asset_id = ? "
                      "ORDER BY h.price_date ASC LIMIT ?";
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool, sql, (const char*[]){uid_str, aid_str, lim_str, NULL});
}

csilk_json_t*
mf_market_repo_get_indices(void* pool, const char* category)
{
    if (!pool) {
        return NULL;
    }
    char cat_str[64] = "";
    if (category && category[0] && strcmp(category, "all") != 0) {
        snprintf(cat_str, sizeof(cat_str), "%s", category);
    }

    csilk_json_t* raw_rows = NULL;
    if (cat_str[0] != '\0') {
        const char* sql = "SELECT symbol, name, category, region_flag, "
                          "current_price, change_amount, change_percent, "
                          "prev_close, open_price, high_price, low_price, "
                          "currency, quote_time, sparkline, "
                          "CAST(updated_at AS TEXT) as updated_at "
                          "FROM market_indices WHERE category = ? "
                          "ORDER BY symbol ASC";
        raw_rows =
            csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, (const char*[]){cat_str, NULL});
    } else {
        const char* sql = "SELECT symbol, name, category, region_flag, "
                          "current_price, change_amount, change_percent, "
                          "prev_close, open_price, high_price, low_price, "
                          "currency, quote_time, sparkline, "
                          "CAST(updated_at AS TEXT) as updated_at "
                          "FROM market_indices "
                          "ORDER BY CASE category "
                          "WHEN 'cn' THEN 1 "
                          "WHEN 'hk' THEN 2 "
                          "WHEN 'us' THEN 3 "
                          "WHEN 'global' THEN 4 "
                          "WHEN 'crypto' THEN 5 "
                          "ELSE 6 END, symbol ASC";
        raw_rows = csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, (const char*[]){NULL});
    }

    if (!raw_rows) {
        return NULL;
    }

    csilk_json_t* out_arr = csilk_json_array();
    if (csilk_json_is_array(raw_rows)) {
        size_t n = csilk_json_array_size(raw_rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(raw_rows, i);
            csilk_json_t* item = csilk_json_object();

            const char* symbol = csilk_json_get_string(row, "symbol");
            const char* name = csilk_json_get_string(row, "name");
            const char* cat = csilk_json_get_string(row, "category");
            const char* flag = csilk_json_get_string(row, "region_flag");
            const char* cur = csilk_json_get_string(row, "currency");
            const char* qtime = csilk_json_get_string(row, "quote_time");
            const char* utime = csilk_json_get_string(row, "updated_at");

            csilk_json_add_string(item, "symbol", symbol ? symbol : "");
            csilk_json_add_string(item, "name", name ? name : "");
            csilk_json_add_string(item, "category", cat ? cat : "");
            csilk_json_add_string(item, "region_flag", flag ? flag : "");

            csilk_json_add_number(item, "current_price", db_get_num(row, "current_price"));
            csilk_json_add_number(item, "change_amount", db_get_num(row, "change_amount"));
            csilk_json_add_number(item, "change_percent", db_get_num(row, "change_percent"));
            csilk_json_add_number(item, "prev_close", db_get_num(row, "prev_close"));
            csilk_json_add_number(item, "open_price", db_get_num(row, "open_price"));
            csilk_json_add_number(item, "high_price", db_get_num(row, "high_price"));
            csilk_json_add_number(item, "low_price", db_get_num(row, "low_price"));

            csilk_json_add_string(item, "currency", cur ? cur : "CNY");
            csilk_json_add_string(item, "quote_time", qtime ? qtime : "");
            csilk_json_add_string(item, "updated_at", utime ? utime : "");

            const char*   sp_str = csilk_json_get_string(row, "sparkline");
            csilk_json_t* sp_arr = NULL;
            if (sp_str && sp_str[0]) {
                sp_arr = csilk_json_parse(sp_str);
            }
            if (!sp_arr) {
                sp_arr = csilk_json_array();
            }
            csilk_json_add_array(item, "sparkline", sp_arr);

            csilk_json_array_append(out_arr, item);
        }
    }
    csilk_json_free(raw_rows);
    return out_arr;
}

int
mf_market_repo_upsert_index(void*       pool,
                            const char* symbol,
                            const char* name,
                            const char* category,
                            const char* region_flag,
                            double      current_price,
                            double      change_amount,
                            double      change_percent,
                            double      prev_close,
                            double      open_price,
                            double      high_price,
                            double      low_price,
                            const char* currency,
                            const char* quote_time,
                            const char* sparkline_json)
{
    if (!pool || !symbol || !symbol[0]) {
        return -1;
    }

    char cp_s[32], ca_s[32], c_pct_s[32], pc_s[32], op_s[32], hp_s[32], lp_s[32];
    snprintf(cp_s, sizeof(cp_s), "%.4f", current_price);
    snprintf(ca_s, sizeof(ca_s), "%.4f", change_amount);
    snprintf(c_pct_s, sizeof(c_pct_s), "%.4f", change_percent);
    snprintf(pc_s, sizeof(pc_s), "%.4f", prev_close);
    snprintf(op_s, sizeof(op_s), "%.4f", open_price);
    snprintf(hp_s, sizeof(hp_s), "%.4f", high_price);
    snprintf(lp_s, sizeof(lp_s), "%.4f", low_price);

    const char* sql =
        "INSERT INTO market_indices ("
        "symbol, name, category, region_flag, current_price, change_amount, change_percent, "
        "prev_close, open_price, high_price, low_price, currency, quote_time, sparkline, "
        "updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(symbol) DO UPDATE SET "
        "name=excluded.name, category=excluded.category, region_flag=excluded.region_flag, "
        "current_price=excluded.current_price, change_amount=excluded.change_amount, "
        "change_percent=excluded.change_percent, prev_close=excluded.prev_close, "
        "open_price=excluded.open_price, high_price=excluded.high_price, "
        "low_price=excluded.low_price, "
        "currency=excluded.currency, quote_time=excluded.quote_time, sparkline=excluded.sparkline, "
        "updated_at=CURRENT_TIMESTAMP";

    const char* params[] = {symbol,
                            name ? name : "",
                            category ? category : "cn",
                            region_flag ? region_flag : "",
                            cp_s,
                            ca_s,
                            c_pct_s,
                            pc_s,
                            op_s,
                            hp_s,
                            lp_s,
                            currency ? currency : "CNY",
                            quote_time ? quote_time : "",
                            sparkline_json ? sparkline_json : "[]",
                            NULL};

    return csilk_db_exec_param((csilk_db_pool_t*)pool, sql, params);
}

csilk_json_t*
mf_market_repo_get_all_exchange_rates(void* pool)
{
    if (!pool) {
        return NULL;
    }
    const char* sql =
        "SELECT target_currency as currency, rate FROM exchange_rate_history "
        "WHERE base_currency = 'CNY' AND rate_date = ("
        "  SELECT MAX(rate_date) FROM exchange_rate_history WHERE base_currency = 'CNY') "
        "UNION "
        "SELECT base_currency as currency, rate FROM exchange_rates WHERE target_currency = 'CNY' "
        "UNION "
        "SELECT target_currency as currency, (1.0 / rate) as rate FROM exchange_rates "
        "WHERE base_currency = 'CNY' AND rate > 0";

    csilk_json_t* rows =
        csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, (const char*[]){NULL});
    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "CNY", 1.0);
    csilk_json_add_number(out, "RMB", 1.0);

    if (rows && csilk_json_is_array(rows)) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(rows, i);
            const char*   cur = csilk_json_get_string(r, "currency");
            double        rate = db_get_num(r, "rate");
            if (cur && cur[0] && rate > 0.0) {
                csilk_json_add_number(out, cur, rate);
            }
        }
    }
    if (rows) {
        csilk_json_free(rows);
    }
    return out;
}
