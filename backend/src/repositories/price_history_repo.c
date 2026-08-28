#include "repositories/price_history_repo.h"
#include <stdio.h>
#include <string.h>

int
price_history_record(csilk_db_pool_t* pool,
                     int64_t          asset_id,
                     const char*      price_date,
                     double           price,
                     const char*      currency)
{
    char aid_str[32], price_str[64];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(price_str, sizeof(price_str), "%.4f", price);
    const char* cur = (currency && currency[0]) ? currency : "CNY";

    if (db_is_postgres()) {
        const char* sql = "INSERT INTO asset_price_history (asset_id, price_date, price, currency) "
                          "VALUES (?, CAST(? AS DATE), CAST(? AS DOUBLE PRECISION), ?) "
                          "ON CONFLICT(asset_id, price_date) DO UPDATE SET price=EXCLUDED.price, "
                          "currency=EXCLUDED.currency";
        const char* params[] = {aid_str, price_date, price_str, cur, NULL};
        return csilk_db_exec_param(pool, sql, params);
    } else {
        const char* sql = "INSERT INTO asset_price_history (asset_id, price_date, price, currency) "
                          "VALUES (?, ?, ?, ?) "
                          "ON CONFLICT(asset_id, price_date) DO UPDATE SET price=excluded.price, "
                          "currency=excluded.currency";
        const char* params[] = {aid_str, price_date, price_str, cur, NULL};
        return csilk_db_exec_param(pool, sql, params);
    }
}

csilk_json_t*
price_history_list_by_asset(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, int limit)
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
    return csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, aid_str, lim_str, NULL});
}
