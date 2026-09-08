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
