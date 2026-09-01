#include "services/market/quote_engine.h"
#include "services/market/quote_driver.h"
#include "csilk/csilk.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char g_market_proxy[256] = {0};

void
quote_engine_set_proxy(const char* proxy)
{
    if (proxy) {
        strncpy(g_market_proxy, proxy, sizeof(g_market_proxy) - 1);
        g_market_proxy[sizeof(g_market_proxy) - 1] = '\0';
    } else {
        g_market_proxy[0] = '\0';
    }
}

const char*
quote_engine_get_proxy(void)
{
    return g_market_proxy;
}

struct curl_buf {
    char*  data;
    size_t len;
    size_t cap;
};

static size_t
curl_write_cb(void* ptr, size_t size, size_t nmemb, void* user_data)
{
    size_t           total = size * nmemb;
    struct curl_buf* buf = (struct curl_buf*)user_data;
    if (buf->len + total + 1 > buf->cap) {
        size_t new_cap = (buf->cap == 0) ? 4096 : buf->cap * 2;
        while (new_cap < buf->len + total + 1) {
            new_cap *= 2;
        }
        char* new_data = (char*)realloc(buf->data, new_cap);
        if (!new_data) {
            return 0;
        }
        buf->data = new_data;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

char*
quote_engine_http_get(const char* url, const char* proxy, int timeout_sec, size_t* out_len)
{
    if (!url) {
        return NULL;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }

    struct curl_buf buf = {NULL, 0, 0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec > 0 ? (long)timeout_sec : 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    const char* px = (proxy && proxy[0]) ? proxy : (g_market_proxy[0] ? g_market_proxy : NULL);
    if (px && px[0]) {
        curl_easy_setopt(curl, CURLOPT_PROXY, px);
    }

    CURLcode res = curl_easy_perform(curl);
    long     http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code < 200 || http_code >= 400) {
        if (buf.data) {
            free(buf.data);
        }
        return NULL;
    }

    if (out_len) {
        *out_len = buf.len;
    }
    return buf.data;
}

int
quote_engine_search(const char* keyword, market_search_item_t* out_items, int max_items)
{
    if (!keyword || !keyword[0] || !out_items || max_items <= 0) {
        return 0;
    }

    int total_found = 0;

    quote_driver_t* drivers[] = {get_eastmoney_driver(),
                                 get_tencent_driver(),
                                 get_crypto_driver(),
                                 get_yahoo_driver(),
                                 NULL};

    for (int d = 0; drivers[d] != NULL && total_found < max_items; d++) {
        quote_driver_t* drv = drivers[d];
        if (drv->search) {
            int remaining = max_items - total_found;
            int n = drv->search(keyword, out_items + total_found, remaining);
            if (n > 0) {
                total_found += n;
            }
        }
    }

    return total_found;
}

int
quote_engine_fetch_quote(const char* symbol, const char* source_type, market_quote_t* out_quote)
{
    if (!symbol || !symbol[0] || !out_quote) {
        return -1;
    }

    quote_driver_t* drivers[] = {get_eastmoney_driver(),
                                 get_tencent_driver(),
                                 get_crypto_driver(),
                                 get_yahoo_driver(),
                                 NULL};

    /* 1. If explicit source_type provided, try that driver first */
    if (source_type && source_type[0]) {
        for (int d = 0; drivers[d] != NULL; d++) {
            if (strcmp(drivers[d]->source_type, source_type) == 0 ||
                strcmp(drivers[d]->name, source_type) == 0) {
                if (drivers[d]->fetch_single && drivers[d]->fetch_single(symbol, out_quote) == 0) {
                    return 0;
                }
            }
        }
    }

    /* 2. Fallback: try all drivers in order */
    for (int d = 0; drivers[d] != NULL; d++) {
        if (drivers[d]->fetch_single && drivers[d]->fetch_single(symbol, out_quote) == 0) {
            return 0;
        }
    }

    return -1;
}

int
quote_engine_test_connection(const char* proxy, char* out_msg, size_t msg_cap, int* out_latency_ms)
{
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Test Tencent market quote for standard index (sh000001) */
    size_t len = 0;
    char*  data = quote_engine_http_get("http://qt.gtimg.cn/q=sh000001", proxy, 5, &len);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    if (out_latency_ms) {
        *out_latency_ms = (int)ms;
    }

    if (data && len > 0) {
        free(data);
        if (out_msg) {
            snprintf(out_msg, msg_cap, "行情网络畅通，延迟 %ld ms", ms);
        }
        return 0;
    }

    if (out_msg) {
        snprintf(out_msg, msg_cap, "连接行情服务器超时或失败");
    }
    return -1;
}
