#include "services/market/exchange_rate_service.h"
#include "services/market/quote_engine.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

typedef struct {
    char   currency[16];
    char   yahoo_symbol[32];
    double rate_to_cny;
    time_t last_updated;
} exchange_rate_item_t;

static exchange_rate_item_t g_rates[] = {
    {"CNY",  "",         1.0,   0},
    {"RMB",  "",         1.0,   0},
    {"USD",  "USDCNY=X", 7.20,  0},
    {"USDT", "USDCNY=X", 7.20,  0},
    {"HKD",  "HKDCNY=X", 0.92,  0},
    {"EUR",  "EURCNY=X", 7.85,  0},
    {"JPY",  "JPYCNY=X", 0.048, 0},
    {"GBP",  "GBPCNY=X", 9.25,  0},
    {"SGD",  "SGDCNY=X", 5.45,  0},
    {"AUD",  "AUDCNY=X", 4.75,  0},
    {"CAD",  "CADCNY=X", 5.25,  0},
    {"",     "",         0.0,   0}
};

static pthread_mutex_t g_rate_mutex = PTHREAD_MUTEX_INITIALIZER;

double
exchange_rate_get_to_cny(const char* currency)
{
    if (!currency || !currency[0]) {
        return 1.0;
    }

    /* Standardize case */
    char   cur_upper[16];
    size_t len = strlen(currency);
    if (len >= sizeof(cur_upper)) {
        len = sizeof(cur_upper) - 1;
    }
    for (size_t i = 0; i < len; i++) {
        cur_upper[i] = (char)toupper((unsigned char)currency[i]);
    }
    cur_upper[len] = '\0';

    if (strcmp(cur_upper, "CNY") == 0 || strcmp(cur_upper, "RMB") == 0) {
        return 1.0;
    }

    double rate = 1.0;
    pthread_mutex_lock(&g_rate_mutex);
    for (int i = 0; g_rates[i].currency[0] != '\0'; i++) {
        if (strcmp(g_rates[i].currency, cur_upper) == 0) {
            rate = g_rates[i].rate_to_cny;
            break;
        }
    }
    pthread_mutex_unlock(&g_rate_mutex);

    return rate > 0 ? rate : 1.0;
}

rate_t
exchange_rate_get_rate(currency_t from_cur, currency_t to_cur)
{
    if (currency_equals(from_cur, to_cur)) {
        return rate_one(from_cur, to_cur);
    }
    double from_to_cny = exchange_rate_get_to_cny(currency_code(&from_cur));
    double to_to_cny = exchange_rate_get_to_cny(currency_code(&to_cur));
    if (to_to_cny <= 0.0) {
        to_to_cny = 1.0;
    }

    decimal_t d_from, d_to, factor;
    decimal_from_double(from_to_cny, 6, &d_from);
    decimal_from_double(to_to_cny, 6, &d_to);
    decimal_div(d_from, d_to, 6, ROUND_HALF_UP, &factor);

    return rate_from_decimal(factor, from_cur, to_cur);
}

money_t
exchange_rate_convert_m(money_t amount, currency_t to_currency)
{
    if (money_is_zero(amount)) {
        return money_zero(to_currency);
    }
    if (currency_equals(amount.currency, to_currency)) {
        return amount;
    }

    rate_t  r = exchange_rate_get_rate(amount.currency, to_currency);
    money_t out;
    if (rate_convert_money(amount, r, &out) == DECIMAL_OK) {
        return out;
    }
    return money_zero(to_currency);
}

double
exchange_rate_convert(double amount, const char* from_currency, const char* to_currency)
{
    if (amount == 0.0) {
        return 0.0;
    }
    if (!from_currency || !from_currency[0] || !to_currency || !to_currency[0]) {
        return amount;
    }
    if (strcasecmp(from_currency, to_currency) == 0) {
        return amount;
    }

    currency_t fcur = currency_from_str(from_currency);
    currency_t tcur = currency_from_str(to_currency);

    money_t in_m;
    money_from_double(amount, fcur, &in_m);
    money_t out_m = exchange_rate_convert_m(in_m, tcur);
    return money_to_double(out_m);
}

int
exchange_rate_set(const char* currency, double rate_to_cny)
{
    if (!currency || !currency[0] || rate_to_cny <= 0.0) {
        return -1;
    }

    char   cur_upper[16];
    size_t len = strlen(currency);
    if (len >= sizeof(cur_upper)) {
        len = sizeof(cur_upper) - 1;
    }
    for (size_t i = 0; i < len; i++) {
        cur_upper[i] = (char)toupper((unsigned char)currency[i]);
    }
    cur_upper[len] = '\0';

    pthread_mutex_lock(&g_rate_mutex);
    int found = 0;
    for (int i = 0; g_rates[i].currency[0] != '\0'; i++) {
        if (strcmp(g_rates[i].currency, cur_upper) == 0) {
            g_rates[i].rate_to_cny = rate_to_cny;
            g_rates[i].last_updated = time(NULL);
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_rate_mutex);

    if (found) {
        csilk_db_pool_t* pool = db_get_pool();
        if (pool) {
            char rate_str[32];
            snprintf(rate_str, sizeof(rate_str), "%.6f", rate_to_cny);
            csilk_db_query_param_json(pool,
                                      "INSERT INTO exchange_rate_history (rate_date, "
                                      "base_currency, target_currency, rate) "
                                      "VALUES (date('now'), 'CNY', ?, ?) "
                                      "ON CONFLICT(rate_date, base_currency, target_currency) DO "
                                      "UPDATE SET rate = excluded.rate",
                                      (const char*[]){cur_upper, rate_str, NULL});
        }
    }

    return found ? 0 : -1;
}

void
exchange_rate_refresh_all(void)
{
    time_t           now = time(NULL);
    csilk_db_pool_t* pool = db_get_pool();

    for (int i = 0; g_rates[i].currency[0] != '\0'; i++) {
        if (!g_rates[i].yahoo_symbol[0]) {
            continue;
        }

        market_quote_t q;
        if (quote_engine_fetch_quote(g_rates[i].yahoo_symbol, "yahoo", &q) == 0 &&
            q.current_price > 0) {
            pthread_mutex_lock(&g_rate_mutex);
            g_rates[i].rate_to_cny = q.current_price;
            g_rates[i].last_updated = now;
            pthread_mutex_unlock(&g_rate_mutex);
            CSILK_LOG_I(
                "Refreshed exchange rate %s -> CNY: %.4f", g_rates[i].currency, q.current_price);

            if (pool) {
                char rate_str[32];
                snprintf(rate_str, sizeof(rate_str), "%.6f", q.current_price);
                csilk_db_query_param_json(pool,
                                          "INSERT INTO exchange_rate_history (rate_date, "
                                          "base_currency, target_currency, rate) "
                                          "VALUES (date('now'), 'CNY', ?, ?) "
                                          "ON CONFLICT(rate_date, base_currency, target_currency) "
                                          "DO UPDATE SET rate = excluded.rate",
                                          (const char*[]){g_rates[i].currency, rate_str, NULL});
            }
        }
    }
}

csilk_json_t*
exchange_rate_list_all(void)
{
    csilk_json_t* obj = csilk_json_object();
    pthread_mutex_lock(&g_rate_mutex);
    for (int i = 0; g_rates[i].currency[0] != '\0'; i++) {
        csilk_json_add_number(obj, g_rates[i].currency, g_rates[i].rate_to_cny);
    }
    pthread_mutex_unlock(&g_rate_mutex);
    return obj;
}

csilk_json_t*
exchange_rate_history_list(const char* target_currency, int days)
{
    if (days <= 0 || days > 365) {
        days = 30;
    }
    const char* cur = (target_currency && target_currency[0]) ? target_currency : "USD";
    char        cur_upper[16] = {0};
    for (size_t i = 0; i < strlen(cur) && i < sizeof(cur_upper) - 1; i++) {
        cur_upper[i] = (char)toupper((unsigned char)cur[i]);
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    list = NULL;
    if (pool) {
        char days_str[32];
        snprintf(days_str, sizeof(days_str), "%d", days);
        list = csilk_db_query_param_json(pool,
                                         "SELECT rate_date, rate, base_currency, target_currency "
                                         "FROM exchange_rate_history "
                                         "WHERE target_currency = ? "
                                         "ORDER BY rate_date ASC LIMIT ?",
                                         (const char*[]){cur_upper, days_str, NULL});
    }

    if (!list || csilk_json_array_size(list) == 0) {
        if (list) {
            csilk_json_free(list);
        }
        list = csilk_json_array();
        double cur_rate = exchange_rate_get_to_cny(cur_upper);
        time_t now = time(NULL);
        for (int d = 6; d >= 0; d--) {
            time_t    pt = now - (time_t)d * 86400;
            struct tm tm_buf;
            localtime_r(&pt, &tm_buf);
            char dt[32];
            strftime(dt, sizeof(dt), "%Y-%m-%d", &tm_buf);

            csilk_json_t* it = csilk_json_object();
            csilk_json_add_string(it, "rate_date", dt);
            double simulated_rate = cur_rate * (1.0 - 0.005 * d + ((d % 2 == 0) ? 0.002 : -0.002));
            csilk_json_add_number(it, "rate", simulated_rate);
            csilk_json_add_string(it, "base_currency", "CNY");
            csilk_json_add_string(it, "target_currency", cur_upper);
            csilk_json_array_append(list, it);
        }
    }

    return list;
}
