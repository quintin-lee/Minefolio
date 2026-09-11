#include "services/market/driver_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 将任意字符串以 NUL 结尾拷贝进固定长度字符数组（自动截断）。 */
static void
mk_strcpy(char* dst, size_t cap, const char* src)
{
    if (!dst || cap == 0) {
        return;
    }
    const char* s = src ? src : "";
    size_t      n = strlen(s);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, s, n);
    dst[n] = '\0';
}

int
market_mock_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* e = getenv("MINEFOLIO_MARKET_MOCK");
        cached = (e && e[0] && strcmp(e, "0") != 0 && strcmp(e, "false") != 0) ? 1 : 0;
    }
    return cached;
}

/* 向 out_items[*n] 追加一条搜索候选（越界则忽略）。 */
static void
mk_add_item(market_search_item_t* out_items,
            int*                  n,
            int                   max_items,
            const char*           symbol,
            const char*           name,
            const char*           source,
            const char*           market_desc,
            double                price,
            const char*           currency)
{
    if (!out_items || !n || *n >= max_items) {
        return;
    }
    market_search_item_t* it = &out_items[*n];
    memset(it, 0, sizeof(*it));
    mk_strcpy(it->symbol, sizeof(it->symbol), symbol);
    mk_strcpy(it->name, sizeof(it->name), name);
    mk_strcpy(it->source, sizeof(it->source), source);
    mk_strcpy(it->market_desc, sizeof(it->market_desc), market_desc);
    mk_strcpy(it->currency, sizeof(it->currency), currency);
    it->current_price = price;
    (*n)++;
}

int
market_mock_search(const char* keyword, market_search_item_t* out_items, int max_items)
{
    if (!keyword || !keyword[0] || !out_items || max_items <= 0) {
        return 0;
    }

    int n = 0;
    if (strstr(keyword, "600519") || strstr(keyword, "茅台")) {
        mk_add_item(
            out_items, &n, max_items, "sh600519", "贵州茅台", "stock_cn", "A股", 1700.0, "CNY");
    }
    if (strstr(keyword, "USDCNY")) {
        mk_add_item(out_items,
                    &n,
                    max_items,
                    "USDCNY=X",
                    "美元/人民币汇率 (USD/CNY)",
                    "forex",
                    "外汇汇率",
                    7.24,
                    "CNY");
    }
    /* 兜底：未知关键词返回一条占位候选，避免离线搜索空结果。 */
    if (n == 0) {
        mk_add_item(
            out_items, &n, max_items, keyword, "Mock Symbol", "mock", "离线行情", 1.0, "CNY");
    }
    return n;
}

int
market_mock_fetch_single(const char* symbol, market_quote_t* out_quote)
{
    if (!symbol || !symbol[0] || !out_quote) {
        return -1;
    }
    memset(out_quote, 0, sizeof(*out_quote));

    const char* name = "Mock";
    const char* src = "mock";
    const char* currency = "CNY";
    double      price = 100.0;

    if (strstr(symbol, "600519")) {
        name = "贵州茅台";
        src = "stock_cn";
        price = 1700.0;
    } else if (strstr(symbol, "USDCNY")) {
        name = "美元/人民币汇率 (USD/CNY)";
        src = "forex";
        price = 7.24;
    }

    mk_strcpy(out_quote->symbol, sizeof(out_quote->symbol), symbol);
    mk_strcpy(out_quote->name, sizeof(out_quote->name), name);
    mk_strcpy(out_quote->source, sizeof(out_quote->source), src);
    mk_strcpy(out_quote->currency, sizeof(out_quote->currency), currency);
    mk_strcpy(out_quote->quote_time, sizeof(out_quote->quote_time), "mock");
    out_quote->current_price = price;
    out_quote->change_percent = 0.0;
    return 0;
}

int
market_mock_test_connection(char* out_msg, size_t msg_cap, int* out_latency_ms)
{
    if (out_latency_ms) {
        *out_latency_ms = 1;
    }
    if (out_msg && msg_cap > 0) {
        snprintf(out_msg, msg_cap, "离线 mock 行情源已启用（MINEFOLIO_MARKET_MOCK）");
    }
    return 0;
}
