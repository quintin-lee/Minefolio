#include "services/market/quote_driver.h"
#include "services/market/quote_engine.h"
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

static struct {
    const char* symbol;
    const char* name;
} g_popular_crypto[] = {
    {"BTC",  "Bitcoin (比特币)" },
    {"ETH",  "Ethereum (以太坊)"},
    {"BNB",  "BNB (币安币)"     },
    {"SOL",  "Solana"           },
    {"XRP",  "Ripple (瑞波币)"  },
    {"DOGE", "Dogecoin (狗狗币)"},
    {"ADA",  "Cardano (艾达币)" },
    {"AVAX", "Avalanche"        },
    {"DOT",  "Polkadot (波卡)"  },
    {"TRX",  "TRON (波场)"      },
    {"LINK", "Chainlink"        },
    {"USDT", "Tether (泰达币)"  },
    {"USDC", "USD Coin"         },
    {NULL,   NULL               }
};

static int
crypto_search(const char* keyword, market_search_item_t* out_items, int max_items)
{
    if (!keyword || !keyword[0] || !out_items || max_items <= 0) {
        return 0;
    }

    char   kw_upper[64];
    size_t klen = strlen(keyword);
    if (klen >= sizeof(kw_upper)) {
        klen = sizeof(kw_upper) - 1;
    }
    for (size_t i = 0; i < klen; i++) {
        kw_upper[i] = toupper((unsigned char)keyword[i]);
    }
    kw_upper[klen] = '\0';

    int found = 0;
    for (int i = 0; g_popular_crypto[i].symbol != NULL && found < max_items; i++) {
        const char* sym = g_popular_crypto[i].symbol;
        const char* name = g_popular_crypto[i].name;

        if (strstr(sym, kw_upper) != NULL || strstr(name, keyword) != NULL) {
            market_search_item_t* res = &out_items[found];
            snprintf(res->symbol, sizeof(res->symbol), "%sUSDT", sym);
            strncpy(res->name, name, sizeof(res->name) - 1);
            strncpy(res->source, "crypto", sizeof(res->source) - 1);
            strncpy(res->currency, "USD", sizeof(res->currency) - 1);
            snprintf(res->market_desc, sizeof(res->market_desc), "加密货币 (USDT计价)");
            res->current_price = 0.0;
            found++;
        }
    }

    return found;
}

void
driver_crypto_normalize_pair(const char* symbol, char* out_pair, size_t out_cap)
{
    if (!out_pair || out_cap == 0) {
        return;
    }
    out_pair[0] = '\0';
    if (!symbol || !symbol[0]) {
        return;
    }

    size_t j = 0;
    for (size_t i = 0; symbol[i] && j + 1 < out_cap; i++) {
        unsigned char ch = (unsigned char)symbol[i];
        if (isalnum(ch)) {
            out_pair[j++] = (char)toupper(ch);
        }
    }
    out_pair[j] = '\0';

    if (j >= 3 && strcmp(out_pair + j - 3, "USD") == 0) {
        bool stable_suffix =
            (j >= 4 && strcmp(out_pair + j - 4, "BUSD") == 0) ||
            (j >= 4 && strcmp(out_pair + j - 4, "TUSD") == 0) ||
            (j >= 4 && strcmp(out_pair + j - 4, "USDP") == 0) ||
            (j >= 5 && strcmp(out_pair + j - 5, "FDUSD") == 0);
        if (!stable_suffix && j + 1 < out_cap) {
            out_pair[j++] = 'T';
            out_pair[j] = '\0';
        }
    } else if (strstr(out_pair, "USDT") == NULL && strstr(out_pair, "USDC") == NULL &&
               strstr(out_pair, "USD") == NULL) {
        strncat(out_pair, "USDT", out_cap - j - 1);
    }
}

static int
crypto_fetch_single(const char* symbol, market_quote_t* out_quote)
{
    if (!symbol || !symbol[0] || !out_quote) {
        return -1;
    }

    char pair[64];
    driver_crypto_normalize_pair(symbol, pair, sizeof(pair));
    if (!pair[0]) {
        return -1;
    }

    char url[256];
    snprintf(url, sizeof(url), "https://api.binance.com/api/v3/ticker/24hr?symbol=%s", pair);

    size_t len = 0;
    char*  body = quote_engine_http_get(url, NULL, 5, &len);
    if (!body || len == 0) {
        return -1;
    }

    csilk_json_t* root = csilk_json_parse(body);
    free(body);
    if (!root) {
        return -1;
    }

    const char* price_str = csilk_json_get_string(root, "lastPrice");
    const char* change_amt_str = csilk_json_get_string(root, "priceChange");
    const char* change_str = csilk_json_get_string(root, "priceChangePercent");
    const char* open_str = csilk_json_get_string(root, "openPrice");
    const char* high_str = csilk_json_get_string(root, "highPrice");
    const char* low_str = csilk_json_get_string(root, "lowPrice");
    const char* prev_str = csilk_json_get_string(root, "prevClosePrice");
    if (!price_str) {
        csilk_json_free(root);
        return -1;
    }

    double price = atof(price_str);
    double change_pct = change_str ? atof(change_str) : 0.0;
    double change_amt = change_amt_str ? atof(change_amt_str) : 0.0;
    double open_price = open_str ? atof(open_str) : 0.0;
    double high_price = high_str ? atof(high_str) : 0.0;
    double low_price = low_str ? atof(low_str) : 0.0;
    double prev_close = prev_str ? atof(prev_str) : 0.0;
    double close_time_ms = db_get_num(root, "closeTime");

    memset(out_quote, 0, sizeof(*out_quote));
    strncpy(out_quote->symbol, symbol, sizeof(out_quote->symbol) - 1);
    strncpy(out_quote->name, symbol, sizeof(out_quote->name) - 1);
    out_quote->current_price = price;
    out_quote->change_amount = change_amt;
    out_quote->change_percent = change_pct;
    out_quote->open_price = open_price;
    out_quote->high_price = high_price;
    out_quote->low_price = low_price;
    out_quote->prev_close = prev_close > 0.0 ? prev_close : (price - change_amt);
    strncpy(out_quote->source, "crypto", sizeof(out_quote->source) - 1);
    strncpy(out_quote->currency, "USD", sizeof(out_quote->currency) - 1);

    time_t    qt_ts = close_time_ms > 1e11 ? (time_t)(close_time_ms / 1000.0) : time(NULL);
    struct tm tm_now;
    localtime_r(&qt_ts, &tm_now);
    strftime(out_quote->quote_time, sizeof(out_quote->quote_time), "%Y-%m-%d %H:%M:%S", &tm_now);

    csilk_json_free(root);
    return price > 0 ? 0 : -1;
}

static quote_driver_t g_crypto_driver = {
    .name = "crypto",
    .source_type = "crypto",
    .search = crypto_search,
    .fetch_single = crypto_fetch_single,
    .fetch_batch = NULL,
};

quote_driver_t*
get_crypto_driver(void)
{
    return &g_crypto_driver;
}
