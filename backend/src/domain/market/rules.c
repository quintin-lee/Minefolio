#include "domain/market/rules.h"
#include <stdio.h>
#include <string.h>

int mf_market_rule_calc_sync_delta(price_t old_price, price_t new_price, quantity_t qty,
                                   currency_t cur, money_t* out_delta) {
    if (!out_delta) return -1;
    *out_delta = money_zero(cur);

    if (!quantity_is_positive(qty)) {
        return 0;
    }

    double old_p = price_to_double(old_price);
    double new_p = price_to_double(new_price);
    if (old_p <= 0.0 || new_p <= 0.0) {
        return 0;
    }

    double q = quantity_to_double(qty);
    double delta_val = (new_p - old_p) * q;
    money_from_double(delta_val, cur, out_delta);
    return 0;
}

int mf_market_rule_convert_currency(money_t src, double rate_to_cny,
                                    currency_t cny_currency, money_t* out_cny) {
    if (!out_cny || rate_to_cny <= 0.0) return -1;
    double src_val = money_to_double(src);
    double converted = src_val * rate_to_cny;
    money_from_double(converted, cny_currency, out_cny);
    return 0;
}

int mf_market_rule_validate_quote(const mf_market_quote_t* q, char* err_buf, size_t err_len) {
    if (!q) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Quote is NULL");
        return -1;
    }
    if (!q->symbol[0]) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Symbol cannot be empty");
        return -1;
    }
    if (price_to_double(q->current_price) <= 0.0) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Current price must be positive");
        return -1;
    }
    return 0;
}

int mf_market_rule_validate_settings(const mf_market_settings_t* s, char* err_buf, size_t err_len) {
    if (!s) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Settings is NULL");
        return -1;
    }
    if (s->market_sync_interval_min < 1) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Sync interval must be at least 1 minute");
        return -1;
    }
    return 0;
}
