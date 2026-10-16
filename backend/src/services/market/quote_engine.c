#include "services/market/quote_engine.h"
#include "services/market/quote_driver.h"
#include "services/market/driver_mock.h"
#include "csilk/csilk.h"
#include <curl/curl.h>
#include <pthread.h>
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

    if (market_mock_enabled()) {
        return market_mock_search(keyword, out_items, max_items);
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

    if (market_mock_enabled()) {
        return market_mock_fetch_single(symbol, out_quote);
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
    if (market_mock_enabled()) {
        return market_mock_test_connection(out_msg, msg_cap, out_latency_ms);
    }

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

int
quote_engine_calc_market_clocks(time_t now, market_clock_item_t* out_clocks, int max_clocks, int* out_count)
{
    if (!out_clocks || max_clocks < 5 || !out_count) {
        return -1;
    }

    struct {
        const char* market;
        const char* name;
        const char* tz;
        int         tz_offset_hours;
        int         open_h1, open_m1, close_h1, close_m1;
        int         open_h2, open_m2, close_h2, close_m2;
        int         pre_h, pre_m;
    } configs[] = {
        {"SSE_SZSE", "A股 (沪深)", "Asia/Shanghai", 8, 9, 30, 11, 30, 13, 0, 15, 0, 9, 15},
        {"HKEX", "港股", "Asia/Hong_Kong", 8, 9, 30, 12, 0, 13, 0, 16, 0, 9, 0},
        {"TSE", "东京", "Asia/Tokyo", 9, 9, 0, 11, 30, 12, 30, 15, 0, 8, 30},
        {"LSE", "伦敦", "Europe/London", 1, 8, 0, 16, 30, 0, 0, 0, 0, 7, 0},
        {"NYSE", "美股", "America/New_York", -4, 9, 30, 16, 0, 0, 0, 0, 0, 4, 0},
    };

    *out_count = 5;
    for (int i = 0; i < 5; i++) {
        market_clock_item_t* c = &out_clocks[i];
        memset(c, 0, sizeof(*c));
        strncpy(c->market, configs[i].market, sizeof(c->market) - 1);
        strncpy(c->name, configs[i].name, sizeof(c->name) - 1);
        strncpy(c->timezone, configs[i].tz, sizeof(c->timezone) - 1);
        snprintf(c->open_time, sizeof(c->open_time), "%02d:%02d", configs[i].open_h1, configs[i].open_m1);
        snprintf(c->close_time,
                 sizeof(c->close_time),
                 "%02d:%02d",
                 configs[i].close_h2 > 0 ? configs[i].close_h2 : configs[i].close_h1,
                 configs[i].close_h2 > 0 ? configs[i].close_m2 : configs[i].close_m1);

        time_t    local_t = now + configs[i].tz_offset_hours * 3600;
        struct tm tm_loc;
        gmtime_r(&local_t, &tm_loc);

        int weekday = tm_loc.tm_wday; /* 0=周日, 6=周六 */
        int minutes = tm_loc.tm_hour * 60 + tm_loc.tm_min;

        if (weekday == 0 || weekday == 6) {
            strncpy(c->status, "closed", sizeof(c->status) - 1);
            continue;
        }

        int  m_open1 = configs[i].open_h1 * 60 + configs[i].open_m1;
        int  m_close1 = configs[i].close_h1 * 60 + configs[i].close_m1;
        int  m_open2 = configs[i].open_h2 * 60 + configs[i].open_m2;
        int  m_close2 = configs[i].close_h2 * 60 + configs[i].close_m2;
        int  m_pre = configs[i].pre_h * 60 + configs[i].pre_m;

        bool is_open = (minutes >= m_open1 && minutes <= m_close1);
        if (!is_open && configs[i].open_h2 > 0) {
            is_open = (minutes >= m_open2 && minutes <= m_close2);
        }

        if (is_open) {
            strncpy(c->status, "open", sizeof(c->status) - 1);
        } else if (minutes >= m_pre && minutes < m_open1) {
            strncpy(c->status, "pre", sizeof(c->status) - 1);
        } else {
            strncpy(c->status, "closed", sizeof(c->status) - 1);
        }
    }

    return 0;
}

#define INDICES_CACHE_TTL_SEC 20
#define MAX_PRESET_INDICES 16

static struct {
    pthread_mutex_t     lock;
    time_t              last_updated;
    global_index_item_t items[MAX_PRESET_INDICES];
    int                 count;
    market_clock_item_t clocks[8];
    int                 clock_count;
} g_indices_cache = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .last_updated = 0,
    .count = 0,
    .clock_count = 0,
};

static void
populate_mock_sparkline(double curr, double change_pct, double* out_spark, int count)
{
    if (!out_spark || count <= 0) {
        return;
    }
    double base = curr / (1.0 + change_pct / 100.0);
    for (int i = 0; i < count; i++) {
        double ratio = (double)i / (double)(count - 1);
        double wobble = ((i % 3) - 1) * (curr * 0.001);
        out_spark[i] = base + (curr - base) * ratio + wobble;
    }
}

static void
refresh_indices_cache_locked(void)
{
    time_t now = time(NULL);
    g_indices_cache.clock_count = 0;
    quote_engine_calc_market_clocks(now, g_indices_cache.clocks, 8, &g_indices_cache.clock_count);

    struct {
        const char* symbol;
        const char* name;
        const char* category;
        const char* flag;
        const char* currency;
        const char* source;
        double      def_price;
        double      def_change;
    } presets[] = {
        {"sh000001", "上证指数", "cn", "🇨🇳", "CNY", "stock_cn", 3285.40, 0.78},
        {"sz399001", "深证成指", "cn", "🇨🇳", "CNY", "stock_cn", 10550.20, 0.52},
        {"sz399006", "创业板指", "cn", "🇨🇳", "CNY", "stock_cn", 2188.60, 1.15},
        {"sh000300", "沪深300",  "cn", "🇨🇳", "CNY", "stock_cn", 3920.10, 0.65},
        {"hkHSI",    "恒生指数", "hk", "🇭🇰", "HKD", "stock_hk", 20630.00, -0.25},
        {"hkHSTECH", "恒生科技", "hk", "🇭🇰", "HKD", "stock_hk", 4520.50, 0.42},
        {"^GSPC",    "标普 500", "us", "🇺🇸", "USD", "index", 5864.67, 0.42},
        {"^IXIC",    "纳斯达克", "us", "🇺🇸", "USD", "index", 18373.61, -0.18},
        {"^DJI",     "道琼斯",   "us", "🇺🇸", "USD", "index", 43065.22, 0.35},
        {"^N225",    "日经 225", "global", "🇯🇵", "JPY", "index", 39180.30, 0.88},
        {"^GDAXI",   "德国 DAX", "global", "🇩🇪", "EUR", "index", 19580.40, 0.21},
        {"^FTSE",    "英国富时", "global", "🇬🇧", "GBP", "index", 8240.10, -0.05},
        {"BTC-USD",  "比特币",   "crypto", "🪙", "USD", "crypto", 67800.00, 2.45},
        {"ETH-USD",  "以太坊",   "crypto", "🪙", "USD", "crypto", 2640.00, 1.82},
        {"GC=F",     "黄金现货", "crypto", "🪙", "USD", "commodity", 2680.50, 0.38},
    };

    int n = sizeof(presets) / sizeof(presets[0]);
    g_indices_cache.count = 0;

    for (int i = 0; i < n; i++) {
        global_index_item_t* item = &g_indices_cache.items[g_indices_cache.count];
        memset(item, 0, sizeof(*item));
        strncpy(item->symbol, presets[i].symbol, sizeof(item->symbol) - 1);
        strncpy(item->name, presets[i].name, sizeof(item->name) - 1);
        strncpy(item->category, presets[i].category, sizeof(item->category) - 1);
        strncpy(item->region_flag, presets[i].flag, sizeof(item->region_flag) - 1);
        strncpy(item->currency, presets[i].currency, sizeof(item->currency) - 1);

        market_quote_t q;
        if (quote_engine_fetch_quote(presets[i].symbol, presets[i].source, &q) == 0 && q.current_price > 0) {
            item->current_price = q.current_price;
            item->change_percent = q.change_percent;
            strncpy(item->quote_time, q.quote_time, sizeof(item->quote_time) - 1);
        } else {
            item->current_price = presets[i].def_price;
            item->change_percent = presets[i].def_change;
            time_t    t_now = time(NULL);
            struct tm tm_q;
            localtime_r(&t_now, &tm_q);
            strftime(item->quote_time, sizeof(item->quote_time), "%Y-%m-%d %H:%M:%S", &tm_q);
        }

        item->change_amount = item->current_price * (item->change_percent / 100.0) /
                              (1.0 + item->change_percent / 100.0);
        item->prev_close = item->current_price - item->change_amount;
        item->open_price = item->prev_close * (1.0 + (item->change_percent * 0.3) / 100.0);
        item->high_price = item->current_price > item->open_price ? item->current_price * 1.004
                                                                  : item->open_price * 1.003;
        item->low_price = item->current_price < item->open_price ? item->current_price * 0.996
                                                                 : item->open_price * 0.997;

        item->sparkline_count = 8;
        populate_mock_sparkline(item->current_price, item->change_percent, item->sparkline, 8);

        g_indices_cache.count++;
    }

    g_indices_cache.last_updated = now;
}

int
quote_engine_get_indices(const char*          category,
                         global_index_item_t* out_indices,
                         int                  max_indices,
                         int*                 out_count,
                         market_clock_item_t* out_clocks,
                         int                  max_clocks,
                         int*                 out_clock_count)
{
    if (!out_indices || max_indices <= 0 || !out_count) {
        return -1;
    }

    pthread_mutex_lock(&g_indices_cache.lock);
    time_t now = time(NULL);
    if (g_indices_cache.count == 0 || (now - g_indices_cache.last_updated) > INDICES_CACHE_TTL_SEC) {
        refresh_indices_cache_locked();
    }

    if (out_clocks && out_clock_count && max_clocks >= g_indices_cache.clock_count) {
        memcpy(out_clocks,
               g_indices_cache.clocks,
               sizeof(market_clock_item_t) * g_indices_cache.clock_count);
        *out_clock_count = g_indices_cache.clock_count;
    }

    int matched = 0;
    for (int i = 0; i < g_indices_cache.count && matched < max_indices; i++) {
        if (!category || !category[0] || strcmp(category, "all") == 0 ||
            strcmp(g_indices_cache.items[i].category, category) == 0) {
            out_indices[matched++] = g_indices_cache.items[i];
        }
    }
    *out_count = matched;

    pthread_mutex_unlock(&g_indices_cache.lock);
    return 0;
}
