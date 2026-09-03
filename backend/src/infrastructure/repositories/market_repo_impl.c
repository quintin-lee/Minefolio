#include "infrastructure/repositories/market_repo_impl.h"
#include "repositories/asset_repo.h"
#include "repositories/price_history_repo.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mf_market_repo_save_exchange_rate(void* pool, const char* currency, double rate) {
    if (!pool || !currency || !currency[0] || rate <= 0.0) return -1;
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

int mf_market_repo_record_price_history(void* pool, int64_t asset_id, const char* date,
                                       price_t price, currency_t cur) {
    if (!pool || asset_id <= 0 || !date || !date[0]) return -1;
    return price_history_record((csilk_db_pool_t*)pool, asset_id, date,
                               price_to_double(price), currency_code(&cur));
}

int mf_market_repo_update_asset_quote(void* pool, int64_t user_id, int64_t asset_id, price_t price) {
    if (!pool || user_id <= 0 || asset_id <= 0) return -1;
    return asset_update_market_quote((csilk_db_pool_t*)pool, user_id, asset_id,
                                    price_to_double(price));
}
