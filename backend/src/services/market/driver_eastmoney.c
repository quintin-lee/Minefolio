#include "services/market/quote_driver.h"
#include "services/market/quote_engine.h"
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int
eastmoney_search(const char* keyword, market_search_item_t* out_items, int max_items)
{
    if (!keyword || !keyword[0] || !out_items || max_items <= 0) {
        return 0;
    }

    char url[512];
    snprintf(url,
             sizeof(url),
             "https://fundsuggest.eastmoney.com/FundSearch/api/FundSearchAPI.ashx?m=1&key=%s",
             keyword);

    size_t len = 0;
    char*  body = quote_engine_http_get(url, NULL, 4, &len);
    if (!body || len == 0) {
        return 0;
    }

    int           found = 0;
    csilk_json_t* root = csilk_json_parse(body);
    free(body);
    if (!root) {
        return 0;
    }

    csilk_json_t* datas = csilk_json_get(root, "Datas");
    if (datas && csilk_json_is_array(datas)) {
        size_t n = csilk_json_array_size(datas);
        for (size_t i = 0; i < n && found < max_items; i++) {
            csilk_json_t* item = csilk_json_array_get(datas, i);
            const char*   code = csilk_json_get_string(item, "CODE");
            const char*   name = csilk_json_get_string(item, "NAME");
            const char*   ftype = csilk_json_get_string(item, "FundBaseInfo.FTYPE");
            if (!code || !name) {
                continue;
            }

            market_search_item_t* res = &out_items[found];
            strncpy(res->symbol, code, sizeof(res->symbol) - 1);
            strncpy(res->name, name, sizeof(res->name) - 1);
            strncpy(res->source, "fund_cn", sizeof(res->source) - 1);
            snprintf(res->market_desc, sizeof(res->market_desc), "公募基金 %s", ftype ? ftype : "");
            res->current_price = 0.0;
            strncpy(res->currency, "CNY", sizeof(res->currency) - 1);
            found++;
        }
    }

    csilk_json_free(root);
    return found;
}

static int
eastmoney_fetch_single(const char* symbol, market_quote_t* out_quote)
{
    if (!symbol || !symbol[0] || !out_quote) {
        return -1;
    }

    /* Extract pure 6-digit numeric code if prefixed */
    const char* code = symbol;
    while (*code && !isdigit((unsigned char)*code)) {
        code++;
    }
    if (!*code) {
        code = symbol;
    }

    char url[256];
    snprintf(url, sizeof(url), "http://fundgz.1234567.com.cn/js/%s.js", code);

    size_t len = 0;
    char*  body = quote_engine_http_get(url, NULL, 5, &len);
    if (!body || len == 0 || strstr(body, "jsonpgz") == NULL) {
        if (body) {
            free(body);
        }
        /* Fallback: try Tencent fund endpoint qt.gtimg.cn/q=jj{code} */
        char jj_sym[32];
        snprintf(jj_sym, sizeof(jj_sym), "jj%s", code);
        return get_tencent_driver()->fetch_single(jj_sym, out_quote);
    }

    /* Response format: jsonpgz({"fundcode":"110011","name":"易方达中小盘混合","jzrq":"2021-09-02","dwjz":"7.5250","gsz":"7.5250","gszzl":"0.00","gztime":"2026-08-28 15:00"}); */
    char* p1 = strchr(body, '(');
    char* p2 = strrchr(body, ')');
    if (!p1 || !p2 || p2 <= p1) {
        free(body);
        return -1;
    }

    *p2 = '\0';
    char* json_str = p1 + 1;

    csilk_json_t* root = csilk_json_parse(json_str);
    if (!root) {
        free(body);
        return -1;
    }

    const char* fcode = csilk_json_get_string(root, "fundcode");
    const char* name = csilk_json_get_string(root, "name");
    const char* dwjz_str = csilk_json_get_string(root, "dwjz");
    const char* gsz_str = csilk_json_get_string(root, "gsz");
    const char* gszzl_str = csilk_json_get_string(root, "gszzl");
    const char* jzrq = csilk_json_get_string(root, "jzrq");
    const char* gztime = csilk_json_get_string(root, "gztime");

    memset(out_quote, 0, sizeof(*out_quote));
    strncpy(out_quote->symbol, fcode ? fcode : symbol, sizeof(out_quote->symbol) - 1);
    strncpy(out_quote->name, name ? name : "", sizeof(out_quote->name) - 1);
    strncpy(out_quote->source, "fund_cn", sizeof(out_quote->source) - 1);
    strncpy(out_quote->currency, "CNY", sizeof(out_quote->currency) - 1);

    double price = 0.0;
    if (gsz_str && atof(gsz_str) > 0) {
        price = atof(gsz_str);
    } else if (dwjz_str && atof(dwjz_str) > 0) {
        price = atof(dwjz_str);
    }
    out_quote->current_price = price;

    if (gszzl_str) {
        out_quote->change_percent = atof(gszzl_str);
    }

    if (gztime && gztime[0]) {
        strncpy(out_quote->quote_time, gztime, sizeof(out_quote->quote_time) - 1);
    } else if (jzrq && jzrq[0]) {
        strncpy(out_quote->quote_time, jzrq, sizeof(out_quote->quote_time) - 1);
    }

    csilk_json_free(root);
    free(body);
    return price > 0 ? 0 : -1;
}

static quote_driver_t g_eastmoney_driver = {
    .name = "eastmoney",
    .source_type = "fund_cn",
    .search = eastmoney_search,
    .fetch_single = eastmoney_fetch_single,
    .fetch_batch = NULL,
};

quote_driver_t*
get_eastmoney_driver(void)
{
    return &g_eastmoney_driver;
}
