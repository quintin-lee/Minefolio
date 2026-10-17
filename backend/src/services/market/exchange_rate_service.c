#include "services/market/exchange_rate_service.h"
#include "services/market/quote_engine.h"
#include "services/market/driver_mock.h"
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

/* date('now') is SQLite-only; CURRENT_DATE is the PG equivalent. The upsert
 * syntax itself (ON CONFLICT ... DO UPDATE) works on both. */
static const char*
exchange_rate_upsert_sql(void)
{
    return db_is_postgres() ? "INSERT INTO exchange_rate_history (rate_date, "
                              "base_currency, target_currency, rate) "
                              "VALUES (CURRENT_DATE, 'CNY', ?, ?) "
                              "ON CONFLICT(rate_date, base_currency, target_currency) DO "
                              "UPDATE SET rate = excluded.rate"
                            : "INSERT INTO exchange_rate_history (rate_date, "
                              "base_currency, target_currency, rate) "
                              "VALUES (date('now'), 'CNY', ?, ?) "
                              "ON CONFLICT(rate_date, base_currency, target_currency) DO "
                              "UPDATE SET rate = excluded.rate";
}

int
exchange_rate_get_to_cny_safe(const char* currency, double* out_rate)
{
    if (!out_rate) {
        return -1;
    }
    *out_rate = 1.0;
    if (!currency || !currency[0]) {
        return -1;
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
        *out_rate = 1.0;
        return 0;
    }

    pthread_mutex_lock(&g_rate_mutex);
    for (int i = 0; g_rates[i].currency[0] != '\0'; i++) {
        if (strcmp(g_rates[i].currency, cur_upper) == 0) {
            *out_rate = g_rates[i].rate_to_cny;
            pthread_mutex_unlock(&g_rate_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_rate_mutex);

    return -1;
}

double
exchange_rate_get_to_cny(const char* currency)
{
    double rate = 1.0;
    if (exchange_rate_get_to_cny_safe(currency, &rate) == 0) {
        return rate > 0.0 ? rate : 1.0;
    }
    if (currency && currency[0] && strcasecmp(currency, "CNY") != 0 && strcasecmp(currency, "RMB") != 0) {
        fprintf(stderr,
                "[WARN] Unknown currency '%s' requested in exchange_rate_get_to_cny; falling back to 1.0\n",
                currency);
    }
    return 1.0;
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
            csilk_db_query_param_json(
                pool, exchange_rate_upsert_sql(), (const char*[]){cur_upper, rate_str, NULL});
        }
    }

    return found ? 0 : -1;
}

/* Single place that writes the in-memory table + history row.
 * Uses exchange_rate_upsert_sql() so both SQLite and PostgreSQL work. */
static void
exchange_rate_store(
    const char* currency, double rate_to_cny, time_t now, csilk_db_pool_t* pool, const char* src)
{
    pthread_mutex_lock(&g_rate_mutex);
    for (int i = 0; g_rates[i].currency[0] != '\0'; i++) {
        if (strcmp(g_rates[i].currency, currency) == 0) {
            g_rates[i].rate_to_cny = rate_to_cny;
            g_rates[i].last_updated = now;
            break;
        }
    }
    pthread_mutex_unlock(&g_rate_mutex);
    CSILK_LOG_I("Refreshed exchange rate %s -> CNY: %.4f (%s)", currency, rate_to_cny, src);

    if (pool) {
        char rate_str[32];
        snprintf(rate_str, sizeof(rate_str), "%.6f", rate_to_cny);
        csilk_db_query_param_json(
            pool, exchange_rate_upsert_sql(), (const char*[]){currency, rate_str, NULL});
    }
}

/* Frankfurter fallback: ECB euro reference rates (EUR base, updated daily).
 * XXX->CNY is derived as EURCNY/EURXXX. Only used for currencies whose
 * fast batch or Yahoo fetch failed. */
static void
exchange_rate_fallback_frankfurter(char             failed[][16],
                                   int              n_failed,
                                   time_t           now,
                                   csilk_db_pool_t* pool)
{
    size_t len = 0;
    char*  body = quote_engine_http_get(
        "https://api.frankfurter.dev/v1/latest?from=EUR&to=USD,CNY,HKD,JPY,GBP,SGD,AUD,CAD",
        NULL,
        4,
        &len);
    if (!body || len == 0) {
        for (int i = 0; i < n_failed; i++) {
            CSILK_LOG_W(
                "Exchange rate %s: yahoo/batch unreachable and frankfurter fallback failed, "
                "keeping stale value",
                failed[i]);
        }
        return;
    }

    csilk_json_t* root = csilk_json_parse(body);
    free(body);
    csilk_json_t* rates = root ? csilk_json_get(root, "rates") : NULL;
    double        cny_per_eur = rates ? db_get_num(rates, "CNY") : 0.0;
    if (!rates || cny_per_eur <= 0) {
        for (int i = 0; i < n_failed; i++) {
            CSILK_LOG_W("Exchange rate %s: frankfurter response invalid, keeping stale value",
                        failed[i]);
        }
        if (root) {
            csilk_json_free(root);
        }
        return;
    }

    for (int i = 0; i < n_failed; i++) {
        /* USDT is pegged to USD; frankfurter codes match ours otherwise. */
        const char* eur_sym = (strcmp(failed[i], "USDT") == 0) ? "USD" : failed[i];
        double      x_per_eur = db_get_num(rates, eur_sym);
        if (x_per_eur > 0) {
            exchange_rate_store(failed[i], cny_per_eur / x_per_eur, now, pool, "frankfurter");
        } else {
            CSILK_LOG_W("Exchange rate %s: no frankfurter cross rate, keeping stale value",
                        failed[i]);
        }
    }
    csilk_json_free(root);
}

static void
exchange_rate_refresh_fast_batch(time_t           now,
                                 csilk_db_pool_t* pool,
                                 char             failed[][16],
                                 int*             out_n_failed)
{
    size_t len = 0;
    char*  body = quote_engine_http_get(
        "http://qt.gtimg.cn/"
        "q=whUSDCNY,whEURCNY,whHKDCNY,whGBPCNY,whSGDCNY,whAUDCNY,whCADCNY,whCNYJPY",
        NULL,
        4,
        &len);
    if (!body || len == 0) {
        if (body) {
            free(body);
        }
        int n = 0;
        for (int i = 0; g_rates[i].currency[0] != '\0'; i++) {
            if (g_rates[i].yahoo_symbol[0] && n < 16) {
                snprintf(failed[n++], sizeof(failed[0]), "%.15s", g_rates[i].currency);
            }
        }
        *out_n_failed = n;
        return;
    }

    char* saveptr = NULL;
    char* line = strtok_r(body, "\n\r", &saveptr);
    while (line) {
        if (strncmp(line, "v_wh", 4) == 0) {
            char* q1 = strchr(line, '"');
            char* q2 = q1 ? strrchr(q1 + 1, '"') : NULL;
            if (q1 && q2 && q2 > q1) {
                *q2 = '\0';
                char* content = q1 + 1;
                char* parts[8] = {0};
                int   part_cnt = 0;
                char* tok_save = NULL;
                char* tok = strtok_r(content, "~", &tok_save);
                while (tok && part_cnt < 8) {
                    parts[part_cnt++] = tok;
                    tok = strtok_r(NULL, "~", &tok_save);
                }
                if (part_cnt >= 4) {
                    double rate = atof(parts[3]);
                    if (rate > 0.0) {
                        if (strstr(line, "whUSDCNY")) {
                            exchange_rate_store("USD", rate, now, pool, "tencent");
                            exchange_rate_store("USDT", rate, now, pool, "tencent");
                        } else if (strstr(line, "whEURCNY")) {
                            exchange_rate_store("EUR", rate, now, pool, "tencent");
                        } else if (strstr(line, "whHKDCNY")) {
                            exchange_rate_store("HKD", rate, now, pool, "tencent");
                        } else if (strstr(line, "whGBPCNY")) {
                            exchange_rate_store("GBP", rate, now, pool, "tencent");
                        } else if (strstr(line, "whSGDCNY")) {
                            exchange_rate_store("SGD", rate, now, pool, "tencent");
                        } else if (strstr(line, "whAUDCNY")) {
                            exchange_rate_store("AUD", rate, now, pool, "tencent");
                        } else if (strstr(line, "whCADCNY")) {
                            exchange_rate_store("CAD", rate, now, pool, "tencent");
                        } else if (strstr(line, "whCNYJPY")) {
                            exchange_rate_store("JPY", 1.0 / rate, now, pool, "tencent");
                        }
                    }
                }
            }
        }
        line = strtok_r(NULL, "\n\r", &saveptr);
    }
    free(body);

    int n = 0;
    pthread_mutex_lock(&g_rate_mutex);
    for (int i = 0; g_rates[i].currency[0] != '\0'; i++) {
        if (!g_rates[i].yahoo_symbol[0]) {
            continue;
        }
        if (g_rates[i].last_updated < now - 5) {
            if (n < 16) {
                snprintf(failed[n++], sizeof(failed[0]), "%.15s", g_rates[i].currency);
            }
        }
    }
    pthread_mutex_unlock(&g_rate_mutex);
    *out_n_failed = n;
}

void
exchange_rate_refresh_all(void)
{
    /* Offline mock mode returns placeholder prices (e.g. 100.0) for FX
     * symbols; never let it overwrite the rate table. */
    if (market_mock_enabled()) {
        return;
    }

    time_t           now = time(NULL);
    csilk_db_pool_t* pool = db_get_pool();
    char             failed[16][16];
    int              n_failed = 0;

    /* 1. Fast batch fetch (<100ms) for all major FX pairs */
    exchange_rate_refresh_fast_batch(now, pool, failed, &n_failed);

    /* 2. Fallback to Frankfurter ECB reference rates for any missing currency */
    if (n_failed > 0) {
        char still_failed[16][16];
        int  n_still_failed = 0;
        exchange_rate_fallback_frankfurter(failed, n_failed, now, pool);

        pthread_mutex_lock(&g_rate_mutex);
        for (int i = 0; i < n_failed; i++) {
            for (int j = 0; g_rates[j].currency[0] != '\0'; j++) {
                if (strcmp(g_rates[j].currency, failed[i]) == 0) {
                    if (g_rates[j].last_updated < now - 5 && n_still_failed < 16) {
                        snprintf(still_failed[n_still_failed++],
                                 sizeof(still_failed[0]),
                                 "%.15s",
                                 failed[i]);
                    }
                    break;
                }
            }
        }
        pthread_mutex_unlock(&g_rate_mutex);

        /* 3. Fallback to Yahoo Finance for any remaining currencies */
        for (int i = 0; i < n_still_failed; i++) {
            for (int j = 0; g_rates[j].currency[0] != '\0'; j++) {
                if (strcmp(g_rates[j].currency, still_failed[i]) == 0 &&
                    g_rates[j].yahoo_symbol[0]) {
                    market_quote_t q;
                    if (quote_engine_fetch_quote(g_rates[j].yahoo_symbol, "yahoo", &q) == 0 &&
                        q.current_price > 0) {
                        exchange_rate_store(
                            g_rates[j].currency, q.current_price, now, pool, "yahoo");
                    }
                    break;
                }
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
