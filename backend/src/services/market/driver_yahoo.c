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
    const char* source;
    const char* desc;
    const char* currency;
} g_yahoo_presets[] = {
    {"USDCNY=X", "美元/人民币汇率 (USD/CNY)", "forex",     "外汇汇率", "CNY"},
    {"HKDCNY=X", "港币/人民币汇率 (HKD/CNY)", "forex",     "外汇汇率", "CNY"},
    {"EURCNY=X", "欧元/人民币汇率 (EUR/CNY)", "forex",     "外汇汇率", "CNY"},
    {"JPYCNY=X", "日元/人民币汇率 (JPY/CNY)", "forex",     "外汇汇率", "CNY"},
    {"GBPCNY=X", "英镑/人民币汇率 (GBP/CNY)", "forex",     "外汇汇率", "CNY"},
    {"GC=F",     "黄金期货 (Gold Futures)",   "commodity", "大宗商品", "USD"},
    {"SI=F",     "白银期货 (Silver Futures)", "commodity", "大宗商品", "USD"},
    {"CL=F",     "原油期货 (Crude Oil WTI)",  "commodity", "大宗商品", "USD"},
    {"^GSPC",    "标普500指数 (S&P 500)",     "index",     "全球指数", "USD"},
    {"^IXIC",    "纳斯达克综合指数 (NASDAQ)", "index",     "全球指数", "USD"},
    {"^DJI",     "道琼斯工业平均指数 (DJI)",  "index",     "全球指数", "USD"},
    {"^N225",    "日经225指数 (Nikkei 225)",  "index",     "全球指数", "JPY"},
    {"^HSI",     "恒生指数 (Hang Seng)",      "index",     "全球指数", "HKD"},
    {NULL,       NULL,                        NULL,        NULL,       NULL }
};

static int
yahoo_search(const char* keyword, market_search_item_t* out_items, int max_items)
{
    if (!keyword || !keyword[0] || !out_items || max_items <= 0) {
        return 0;
    }

    int found = 0;

    /* 1. Check built-in preset keywords */
    char   kw_upper[64];
    size_t klen = strlen(keyword);
    if (klen >= sizeof(kw_upper)) {
        klen = sizeof(kw_upper) - 1;
    }
    for (size_t i = 0; i < klen; i++) {
        kw_upper[i] = toupper((unsigned char)keyword[i]);
    }
    kw_upper[klen] = '\0';

    for (int i = 0; g_yahoo_presets[i].symbol != NULL && found < max_items; i++) {
        const char* sym = g_yahoo_presets[i].symbol;
        const char* name = g_yahoo_presets[i].name;
        if (strstr(sym, kw_upper) != NULL || strstr(name, keyword) != NULL) {
            market_search_item_t* res = &out_items[found];
            strncpy(res->symbol, sym, sizeof(res->symbol) - 1);
            strncpy(res->name, name, sizeof(res->name) - 1);
            strncpy(res->source, g_yahoo_presets[i].source, sizeof(res->source) - 1);
            strncpy(res->currency, g_yahoo_presets[i].currency, sizeof(res->currency) - 1);
            strncpy(res->market_desc, g_yahoo_presets[i].desc, sizeof(res->market_desc) - 1);
            res->current_price = 0.0;
            found++;
        }
    }

    /* 2. If space remaining, query Yahoo Finance search API */
    if (found < max_items) {
        char url[512];
        snprintf(
            url,
            sizeof(url),
            "https://query2.finance.yahoo.com/v1/finance/search?q=%s&quotesCount=8&newsCount=0",
            keyword);

        size_t len = 0;
        char*  body = quote_engine_http_get(url, NULL, 5, &len);
        if (body && len > 0) {
            csilk_json_t* root = csilk_json_parse(body);
            free(body);
            if (root) {
                csilk_json_t* quotes = csilk_json_get(root, "quotes");
                if (quotes && csilk_json_is_array(quotes)) {
                    size_t n = csilk_json_array_size(quotes);
                    for (size_t i = 0; i < n && found < max_items; i++) {
                        csilk_json_t* q = csilk_json_array_get(quotes, i);
                        const char*   sym = csilk_json_get_string(q, "symbol");
                        const char*   sname = csilk_json_get_string(q, "shortname");
                        const char*   lname = csilk_json_get_string(q, "longname");
                        const char*   qtype = csilk_json_get_string(q, "quoteType");
                        if (!sym || !sym[0]) {
                            continue;
                        }

                        /* Avoid duplicate if already in found list */
                        bool dup = false;
                        for (int j = 0; j < found; j++) {
                            if (strcmp(out_items[j].symbol, sym) == 0) {
                                dup = true;
                                break;
                            }
                        }
                        if (dup) {
                            continue;
                        }

                        market_search_item_t* res = &out_items[found];
                        strncpy(res->symbol, sym, sizeof(res->symbol) - 1);
                        const char* disp_name = sname ? sname : (lname ? lname : sym);
                        strncpy(res->name, disp_name, sizeof(res->name) - 1);
                        strncpy(res->source, "yahoo", sizeof(res->source) - 1);
                        snprintf(res->market_desc,
                                 sizeof(res->market_desc),
                                 "Yahoo %s",
                                 qtype ? qtype : "");
                        res->current_price = 0.0;
                        strncpy(res->currency, "USD", sizeof(res->currency) - 1);
                        found++;
                    }
                }
                csilk_json_free(root);
            }
        }
    }

    return found;
}

static int
yahoo_fetch_single(const char* symbol, market_quote_t* out_quote)
{
    if (!symbol || !symbol[0] || !out_quote) {
        return -1;
    }

    char url[512];
    snprintf(url,
             sizeof(url),
             "https://query1.finance.yahoo.com/v8/finance/chart/%s?interval=1d&range=1d",
             symbol);

    size_t len = 0;
    char*  body = quote_engine_http_get(url, NULL, 6, &len);
    if (!body || len == 0) {
        return -1;
    }

    csilk_json_t* root = csilk_json_parse(body);
    free(body);
    if (!root) {
        return -1;
    }

    csilk_json_t* chart = csilk_json_get(root, "chart");
    if (!chart) {
        csilk_json_free(root);
        return -1;
    }
    csilk_json_t* result = csilk_json_get(chart, "result");
    if (!result || !csilk_json_is_array(result) || csilk_json_array_size(result) == 0) {
        csilk_json_free(root);
        return -1;
    }

    csilk_json_t* item0 = csilk_json_array_get(result, 0);
    csilk_json_t* meta = csilk_json_get(item0, "meta");
    if (!meta) {
        csilk_json_free(root);
        return -1;
    }

    double price = db_get_num(meta, "regularMarketPrice");
    if (price <= 0) {
        price = db_get_num(meta, "chartPreviousClose");
    }
    if (price <= 0) {
        csilk_json_free(root);
        return -1;
    }

    double prev_close = db_get_num(meta, "chartPreviousClose");
    if (prev_close <= 0) {
        prev_close = db_get_num(meta, "previousClose");
    }
    double change_pct = 0.0;
    if (prev_close > 0) {
        change_pct = ((price - prev_close) / prev_close) * 100.0;
    }

    const char* cur = csilk_json_get_string(meta, "currency");
    const char* sname = csilk_json_get_string(meta, "shortName");
    const char* sym = csilk_json_get_string(meta, "symbol");

    memset(out_quote, 0, sizeof(*out_quote));
    strncpy(out_quote->symbol, sym ? sym : symbol, sizeof(out_quote->symbol) - 1);
    strncpy(out_quote->name, sname ? sname : (sym ? sym : symbol), sizeof(out_quote->name) - 1);
    strncpy(out_quote->source, "yahoo", sizeof(out_quote->source) - 1);
    out_quote->current_price = price;
    out_quote->change_percent = change_pct;
    strncpy(out_quote->currency, cur ? cur : "USD", sizeof(out_quote->currency) - 1);

    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(out_quote->quote_time, sizeof(out_quote->quote_time), "%Y-%m-%d %H:%M:%S", &tm_now);

    csilk_json_free(root);
    return 0;
}

static quote_driver_t g_yahoo_driver = {.name = "Yahoo Finance",
                                        .source_type = "yahoo",
                                        .search = yahoo_search,
                                        .fetch_single = yahoo_fetch_single,
                                        .fetch_batch = NULL};

quote_driver_t*
get_yahoo_driver(void)
{
    return &g_yahoo_driver;
}
