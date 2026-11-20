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

static int
crypto_fetch_single(const char* symbol, market_quote_t* out_quote)
{
    if (!symbol || !symbol[0] || !out_quote) {
        return -1;
    }

    char   pair[64];
    size_t slen = strlen(symbol);
    if (slen >= sizeof(pair)) {
        slen = sizeof(pair) - 1;
    }
    for (size_t i = 0; i < slen; i++) {
        pair[i] = toupper((unsigned char)symbol[i]);
    }
    pair[slen] = '\0';

    if (strstr(pair, "USDT") == NULL && strstr(pair, "USD") == NULL) {
        strncat(pair, "USDT", sizeof(pair) - strlen(pair) - 1);
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
    const char* change_str = csilk_json_get_string(root, "priceChangePercent");
    if (!price_str) {
        csilk_json_free(root);
        return -1;
    }

    double price = atof(price_str);
    double change_pct = change_str ? atof(change_str) : 0.0;

    memset(out_quote, 0, sizeof(*out_quote));
    strncpy(out_quote->symbol, symbol, sizeof(out_quote->symbol) - 1);
    strncpy(out_quote->name, symbol, sizeof(out_quote->name) - 1);
    out_quote->current_price = price;
    out_quote->change_percent = change_pct;
    strncpy(out_quote->source, "crypto", sizeof(out_quote->source) - 1);
    strncpy(out_quote->currency, "USD", sizeof(out_quote->currency) - 1);

    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
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
