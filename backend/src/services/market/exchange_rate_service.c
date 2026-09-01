#include "services/market/exchange_rate_service.h"
#include "services/market/quote_engine.h"
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

void
exchange_rate_refresh_all(void)
{
    time_t now = time(NULL);

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
