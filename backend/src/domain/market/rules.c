#include "domain/market/rules.h"
#include "core/financial/rate.h"
#include <stdio.h>
#include <string.h>

int
mf_market_rule_calc_sync_delta(
    price_t old_price, price_t new_price, quantity_t qty, currency_t cur, money_t* out_delta)
{
    if (!out_delta) {
        return -1;
    }
    *out_delta = money_zero(cur);

    if (!quantity_is_positive(qty)) {
        return 0;
    }

    if (!decimal_is_positive(old_price.unit_price) || !decimal_is_positive(new_price.unit_price)) {
        return 0;
    }

    money_t old_val, new_val;
    if (price_times_quantity(old_price, qty, &old_val) != DECIMAL_OK ||
        price_times_quantity(new_price, qty, &new_val) != DECIMAL_OK) {
        return -1;
    }

    money_sub(new_val, old_val, out_delta);
    return 0;
}

int
mf_market_rule_convert_currency(money_t    src,
                                double     rate_to_cny,
                                currency_t cny_currency,
                                money_t*   out_cny)
{
    if (!out_cny || rate_to_cny <= 0.0) {
        return -1;
    }
    rate_t r;
    if (rate_from_double(rate_to_cny, 6, src.currency, cny_currency, &r) != DECIMAL_OK) {
        return -1;
    }
    return (rate_convert_money(src, r, out_cny) == DECIMAL_OK) ? 0 : -1;
}

int
mf_market_rule_validate_quote(const mf_market_quote_t* q, char* err_buf, size_t err_len)
{
    if (!q) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "Quote is NULL");
        }
        return -1;
    }
    if (!q->symbol[0]) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "Symbol cannot be empty");
        }
        return -1;
    }
    if (!decimal_is_positive(q->current_price.unit_price)) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "Current price must be positive");
        }
        return -1;
    }
    return 0;
}

int
mf_market_rule_validate_settings(const mf_market_settings_t* s, char* err_buf, size_t err_len)
{
    if (!s) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "Settings is NULL");
        }
        return -1;
    }
    if (s->market_sync_interval_min < 1) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "Sync interval must be at least 1 minute");
        }
        return -1;
    }
    return 0;
}
