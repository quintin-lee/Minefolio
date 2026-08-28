#include "services/market/quote_driver.h"
#include "services/market/quote_engine.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <iconv.h>

static void
gbk_to_utf8(const char* in, char* out, size_t out_cap)
{
    if (!in || !out || out_cap == 0) {
        return;
    }
    iconv_t cd = iconv_open("UTF-8//IGNORE", "GBK");
    if (cd == (iconv_t)-1) {
        strncpy(out, in, out_cap - 1);
        out[out_cap - 1] = '\0';
        return;
    }
    char*  in_ptr = (char*)in;
    size_t in_bytes = strlen(in);
    char*  out_ptr = out;
    size_t out_bytes = out_cap - 1;
    iconv(cd, &in_ptr, &in_bytes, &out_ptr, &out_bytes);
    *out_ptr = '\0';
    iconv_close(cd);
}

/* Helper to normalize Tencent symbol: 600519 -> sh600519, 000001 -> sz000001 */
static void
normalize_tencent_symbol(const char* in, char* out, size_t out_cap)
{
    if (!in || !out || out_cap == 0) {
        return;
    }
    if (strncmp(in, "sh", 2) == 0 || strncmp(in, "sz", 2) == 0 || strncmp(in, "bj", 2) == 0 ||
        strncmp(in, "hk", 2) == 0 || strncmp(in, "us", 2) == 0 || strncmp(in, "jj", 2) == 0 ||
        strncmp(in, "s_", 2) == 0 || strncmp(in, "hf_", 3) == 0) {
        strncpy(out, in, out_cap - 1);
        out[out_cap - 1] = '\0';
        return;
    }

    /* Check if 6 digits starting with 6/9 -> sh, 0/3 -> sz, 4/8 -> bj */
    if (strlen(in) == 6 && isdigit((unsigned char)in[0])) {
        if (in[0] == '6' || in[0] == '9' || in[0] == '5') {
            snprintf(out, out_cap, "sh%s", in);
        } else if (in[0] == '8' || in[0] == '4') {
            snprintf(out, out_cap, "bj%s", in);
        } else {
            snprintf(out, out_cap, "sz%s", in);
        }
        return;
    }

    /* Check if US stock ticker (alphabetic, e.g. AAPL, TSLA) */
    bool is_alpha = true;
    for (int i = 0; in[i]; i++) {
        if (!isalpha((unsigned char)in[i]) && in[i] != '.') {
            is_alpha = false;
            break;
        }
    }
    if (is_alpha && strlen(in) <= 6) {
        snprintf(out, out_cap, "us%s", in);
        return;
    }

    /* Fallback */
    strncpy(out, in, out_cap - 1);
    out[out_cap - 1] = '\0';
}

static int
tencent_search(const char* keyword, market_search_item_t* out_items, int max_items)
{
    if (!keyword || !keyword[0] || !out_items || max_items <= 0) {
        return 0;
    }

    char url[512];
    snprintf(url, sizeof(url), "https://smartbox.gtimg.cn/s3/?t=all&q=%s", keyword);

    size_t len = 0;
    char*  body = quote_engine_http_get(url, NULL, 4, &len);
    if (!body || len == 0) {
        return 0;
    }

    /* Format: v_hint="sh~600519~贵州茅台~gzmt~GP-A^sz~000001~平安银行~payh~GP-A"; */
    char* p1 = strchr(body, '"');
    char* p2 = strrchr(body, '"');
    if (!p1 || !p2 || p2 <= p1) {
        free(body);
        return 0;
    }

    *p2 = '\0';
    char* text = p1 + 1;

    int   found = 0;
    char* saveptr1 = NULL;
    char* line = strtok_r(text, "^", &saveptr1);
    while (line && found < max_items) {
        /* Line format: market~code~name~pinyin~type */
        char* parts[8] = {0};
        int   part_count = 0;
        char* saveptr2 = NULL;
        char* token = strtok_r(line, "~", &saveptr2);
        while (token && part_count < 8) {
            parts[part_count++] = token;
            token = strtok_r(NULL, "~", &saveptr2);
        }

        if (part_count >= 3) {
            const char* mkt = parts[0];
            const char* code = parts[1];
            const char* name = parts[2];
            const char* type_desc = part_count >= 5 ? parts[4] : "";

            market_search_item_t* res = &out_items[found];
            if (strcmp(mkt, "us") == 0) {
                snprintf(res->symbol, sizeof(res->symbol), "us%s", code);
                strncpy(res->source, "stock_us", sizeof(res->source) - 1);
                strncpy(res->currency, "USD", sizeof(res->currency) - 1);
                snprintf(res->market_desc, sizeof(res->market_desc), "美股 %s", type_desc);
            } else if (strcmp(mkt, "hk") == 0) {
                snprintf(res->symbol, sizeof(res->symbol), "hk%s", code);
                strncpy(res->source, "stock_hk", sizeof(res->source) - 1);
                strncpy(res->currency, "HKD", sizeof(res->currency) - 1);
                snprintf(res->market_desc, sizeof(res->market_desc), "港股 %s", type_desc);
            } else if (strcmp(mkt, "jj") == 0) {
                snprintf(res->symbol, sizeof(res->symbol), "%s", code);
                strncpy(res->source, "fund_cn", sizeof(res->source) - 1);
                strncpy(res->currency, "CNY", sizeof(res->currency) - 1);
                snprintf(res->market_desc, sizeof(res->market_desc), "公募基金 %s", type_desc);
            } else {
                snprintf(res->symbol, sizeof(res->symbol), "%s%s", mkt, code);
                strncpy(res->source, "stock_cn", sizeof(res->source) - 1);
                strncpy(res->currency, "CNY", sizeof(res->currency) - 1);
                snprintf(res->market_desc, sizeof(res->market_desc), "A股/ETF %s", type_desc);
            }

            /* Convert name and type_desc from GBK/raw if needed, or copy */
            gbk_to_utf8(name, res->name, sizeof(res->name));
            if (!res->name[0]) {
                strncpy(res->name, name, sizeof(res->name) - 1);
            }

            res->current_price = 0.0;
            found++;
        }
        line = strtok_r(NULL, "^", &saveptr1);
    }

    free(body);
    return found;
}

static int
parse_tencent_line(const char* line, market_quote_t* out_quote)
{
    /* Format stock: v_sh600519="1~贵州茅台~600519~1420.50~1415.00~1418.00~...~+1.52~..."; */
    /* Format fund:  v_jj110011="110011~易方达精选~0.0000~0.0000~~4.2064~5.9964~-0.1638~2026-08-27~"; */
    const char* p1 = strchr(line, '"');
    const char* p2 = strrchr(line, '"');
    if (!p1 || !p2 || p2 <= p1) {
        return -1;
    }

    bool is_fund_line = (strstr(line, "v_jj") != NULL);

    char   buf[1024];
    size_t content_len = p2 - (p1 + 1);
    if (content_len >= sizeof(buf)) {
        content_len = sizeof(buf) - 1;
    }
    memcpy(buf, p1 + 1, content_len);
    buf[content_len] = '\0';

    char* parts[64] = {0};
    int   count = 0;
    char* saveptr = NULL;
    char* token = strtok_r(buf, "~", &saveptr);
    while (token && count < 64) {
        parts[count++] = token;
        token = strtok_r(NULL, "~", &saveptr);
    }

    if (is_fund_line) {
        if (count < 6) {
            return -1;
        }
        const char* code = parts[0];
        const char* raw_name = parts[1];
        double      price = count > 5 ? atof(parts[4]) : 0.0; /* net value */
        if (price <= 0.0 && count > 6) {
            price = atof(parts[5]);
        }
        double      change_pct = count > 7 ? atof(parts[6]) : 0.0;
        const char* date_time = count > 8 ? parts[7] : "";

        memset(out_quote, 0, sizeof(*out_quote));
        strncpy(out_quote->symbol, code, sizeof(out_quote->symbol) - 1);
        gbk_to_utf8(raw_name, out_quote->name, sizeof(out_quote->name));
        out_quote->current_price = price;
        out_quote->change_percent = change_pct;
        strncpy(out_quote->source, "fund_cn", sizeof(out_quote->source) - 1);
        strncpy(out_quote->currency, "CNY", sizeof(out_quote->currency) - 1);
        strncpy(out_quote->quote_time, date_time, sizeof(out_quote->quote_time) - 1);
        return price > 0 ? 0 : -1;
    }

    if (count < 4) {
        return -1;
    }

    const char* raw_name = parts[1];
    const char* code = parts[2];
    double      price = atof(parts[3]);
    double      change_pct = count > 32 ? atof(parts[32]) : 0.0;
    const char* date_time = count > 30 ? parts[30] : "";

    memset(out_quote, 0, sizeof(*out_quote));
    strncpy(out_quote->symbol, code, sizeof(out_quote->symbol) - 1);
    gbk_to_utf8(raw_name, out_quote->name, sizeof(out_quote->name));
    out_quote->current_price = price;
    out_quote->change_percent = change_pct;
    strncpy(out_quote->source, "stock_cn", sizeof(out_quote->source) - 1);
    strncpy(out_quote->currency, "CNY", sizeof(out_quote->currency) - 1);

    if (date_time && strlen(date_time) >= 8) {
        if (strlen(date_time) >= 14) {
            snprintf(out_quote->quote_time,
                     sizeof(out_quote->quote_time),
                     "%.4s-%.2s-%.2s %.2s:%.2s:%.2s",
                     date_time,
                     date_time + 4,
                     date_time + 6,
                     date_time + 8,
                     date_time + 10,
                     date_time + 12);
        } else {
            strncpy(out_quote->quote_time, date_time, sizeof(out_quote->quote_time) - 1);
        }
    }

    return price > 0 ? 0 : -1;
}

static int
tencent_fetch_single(const char* symbol, market_quote_t* out_quote)
{
    if (!symbol || !symbol[0] || !out_quote) {
        return -1;
    }

    char norm_sym[64];
    normalize_tencent_symbol(symbol, norm_sym, sizeof(norm_sym));

    char url[256];
    snprintf(url, sizeof(url), "http://qt.gtimg.cn/q=%s", norm_sym);

    size_t len = 0;
    char*  body = quote_engine_http_get(url, NULL, 5, &len);
    if (!body || len == 0) {
        return -1;
    }

    int res = parse_tencent_line(body, out_quote);
    if (res == 0) {
        strncpy(out_quote->symbol, symbol, sizeof(out_quote->symbol) - 1);
    }
    free(body);
    return res;
}

static int
tencent_fetch_batch(const char** symbols, int count, market_quote_t* out_quotes, int* out_count)
{
    if (!symbols || count <= 0 || !out_quotes || !out_count) {
        return -1;
    }

    char   query[2048] = {0};
    size_t qlen = 0;
    for (int i = 0; i < count; i++) {
        char norm[64];
        normalize_tencent_symbol(symbols[i], norm, sizeof(norm));
        if (i > 0) {
            strncat(query, ",", sizeof(query) - qlen - 1);
            qlen++;
        }
        strncat(query, norm, sizeof(query) - qlen - 1);
        qlen += strlen(norm);
    }

    char url[2560];
    snprintf(url, sizeof(url), "http://qt.gtimg.cn/q=%s", query);

    size_t len = 0;
    char*  body = quote_engine_http_get(url, NULL, 8, &len);
    if (!body || len == 0) {
        return -1;
    }

    int   parsed_count = 0;
    char* saveptr = NULL;
    char* line = strtok_r(body, ";", &saveptr);
    while (line && parsed_count < count) {
        while (*line == '\r' || *line == '\n' || *line == ' ') {
            line++;
        }
        if (*line) {
            market_quote_t q;
            if (parse_tencent_line(line, &q) == 0) {
                out_quotes[parsed_count++] = q;
            }
        }
        line = strtok_r(NULL, ";", &saveptr);
    }

    free(body);
    *out_count = parsed_count;
    return parsed_count > 0 ? 0 : -1;
}

static quote_driver_t g_tencent_driver = {
    .name = "tencent",
    .source_type = "stock_cn",
    .search = tencent_search,
    .fetch_single = tencent_fetch_single,
    .fetch_batch = tencent_fetch_batch,
};

quote_driver_t*
get_tencent_driver(void)
{
    return &g_tencent_driver;
}
