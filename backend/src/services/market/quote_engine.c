#include "services/market/quote_engine.h"
#include "services/market/quote_driver.h"
#include "services/market/driver_mock.h"
#include "infrastructure/repositories/market_repo_impl.h"
#include "core/scheduler/scheduler_events.h"
#include "common/http_safety.h"
#include "csilk/csilk.h"
#include <curl/curl.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static char            g_market_proxy[256] = {0};
static pthread_mutex_t g_proxy_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool     g_quote_stop = false;

void
quote_engine_request_stop(void)
{
    atomic_store(&g_quote_stop, true);
}

void
quote_engine_reset_stop(void)
{
    atomic_store(&g_quote_stop, false);
}

static int
quote_curl_xferinfo_cb(
    void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)clientp;
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    return atomic_load(&g_quote_stop) ? 1 : 0;
}

void
quote_engine_set_proxy(const char* proxy)
{
    pthread_mutex_lock(&g_proxy_mutex);
    if (proxy) {
        strncpy(g_market_proxy, proxy, sizeof(g_market_proxy) - 1);
        g_market_proxy[sizeof(g_market_proxy) - 1] = '\0';
    } else {
        g_market_proxy[0] = '\0';
    }
    pthread_mutex_unlock(&g_proxy_mutex);
}

const char*
quote_engine_get_proxy(void)
{
    _Thread_local static char tls_proxy[256];
    pthread_mutex_lock(&g_proxy_mutex);
    strncpy(tls_proxy, g_market_proxy, sizeof(tls_proxy) - 1);
    tls_proxy[sizeof(tls_proxy) - 1] = '\0';
    pthread_mutex_unlock(&g_proxy_mutex);
    return tls_proxy;
}

char*
quote_engine_http_get(const char* url, const char* proxy, int timeout_sec, size_t* out_len)
{
    if (!url || atomic_load(&g_quote_stop)) {
        return NULL;
    }

    char        local_proxy[256] = {0};
    const char* px = NULL;
    if (proxy && proxy[0]) {
        px = proxy;
    } else {
        pthread_mutex_lock(&g_proxy_mutex);
        if (g_market_proxy[0]) {
            strncpy(local_proxy, g_market_proxy, sizeof(local_proxy) - 1);
            px = local_proxy;
        }
        pthread_mutex_unlock(&g_proxy_mutex);
    }

    char resolve_entry[320] = {0};
    if (mf_http_validate_and_resolve_url(
            url, 2048, false, (px && px[0]), resolve_entry, sizeof(resolve_entry)) != 0) {
        return NULL;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }

    mf_curl_buf_t      buf = {.data = NULL, .size = 0, .cap = 0, .max_bytes = MF_CURL_MAX_4MB};
    struct curl_slist* resolve_list = mf_http_curl_apply_security(curl, resolve_entry, px, true);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, quote_curl_xferinfo_cb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec > 0 ? (long)timeout_sec : 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mf_curl_bounded_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");

    /* SSL / TLS Verification: strict peer and host verification by default */
    long verify_peer = 1L;
    long verify_host = 2L;

    const char* insecure_env = getenv("MINEFOLIO_ALLOW_INSECURE_SSL");
    if (insecure_env && (strcmp(insecure_env, "1") == 0 || strcasecmp(insecure_env, "true") == 0)) {
        verify_peer = 0L;
        verify_host = 0L;
        static atomic_bool s_warned_insecure_ssl = false;
        if (!atomic_exchange(&s_warned_insecure_ssl, true)) {
            fprintf(stderr,
                    "[SECURITY WARNING] quote_engine: Insecure SSL verification is explicitly "
                    "enabled via MINEFOLIO_ALLOW_INSECURE_SSL!\n");
        }
    }
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_peer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_host);

    /* Optional custom CA bundle path (e.g. for enterprise proxy or self-hosted CA) */
    const char* custom_ca = getenv("MINEFOLIO_CA_BUNDLE");
    if (!custom_ca || !custom_ca[0]) {
        custom_ca = getenv("SSL_CERT_FILE");
    }
    if (custom_ca && custom_ca[0]) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, custom_ca);
    }

    if (px && px[0]) {
        curl_easy_setopt(curl, CURLOPT_PROXY, px);
    }

    CURLcode res = curl_easy_perform(curl);
    long     http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (resolve_list) {
        curl_slist_free_all(resolve_list);
    }
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code < 200 || http_code >= 400) {
        if (buf.data) {
            free(buf.data);
        }
        return NULL;
    }

    if (out_len) {
        *out_len = buf.size;
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

static bool
driver_matches_source(const quote_driver_t* drv, const char* source_type)
{
    if (!drv || !source_type || !source_type[0]) {
        return false;
    }
    if (strcmp(drv->source_type, source_type) == 0 || strcmp(drv->name, source_type) == 0) {
        return true;
    }
    if (strcmp(drv->source_type, "crypto") == 0 &&
        (strcasecmp(source_type, "binance") == 0 || strcasecmp(source_type, "okx") == 0)) {
        return true;
    }
    if (strcmp(drv->source_type, "stock_cn") == 0 &&
        (strcasecmp(source_type, "stock_hk") == 0 || strcasecmp(source_type, "stock_us") == 0 ||
         strcasecmp(source_type, "tencent") == 0)) {
        return true;
    }
    if (strcmp(drv->source_type, "fund_cn") == 0 &&
        (strcasecmp(source_type, "fund") == 0 || strcasecmp(source_type, "eastmoney") == 0)) {
        return true;
    }
    if (strcmp(drv->source_type, "yahoo") == 0 &&
        (strcasecmp(source_type, "forex") == 0 || strcasecmp(source_type, "commodity") == 0 ||
         strcasecmp(source_type, "index") == 0)) {
        return true;
    }
    return false;
}

int
quote_engine_fetch_quote(const char* symbol, const char* source_type, market_quote_t* out_quote)
{
    if (!symbol || !symbol[0] || !out_quote) {
        return -1;
    }

    if (atomic_load(&g_quote_stop)) {
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
            if (driver_matches_source(drivers[d], source_type)) {
                if (drivers[d]->fetch_single && drivers[d]->fetch_single(symbol, out_quote) == 0) {
                    if (out_quote->change_percent >= 5.0 || out_quote->change_percent <= -5.0) {
                        scheduler_event_publish_price_spike(0, symbol, out_quote->change_percent);
                    }
                    return 0;
                }
            }
        }
    }

    /* 2. Fallback: try all drivers in order */
    for (int d = 0; drivers[d] != NULL; d++) {
        if (drivers[d]->fetch_single && drivers[d]->fetch_single(symbol, out_quote) == 0) {
            if (out_quote->change_percent >= 5.0 || out_quote->change_percent <= -5.0) {
                scheduler_event_publish_price_spike(0, symbol, out_quote->change_percent);
            }
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
    char*  data = quote_engine_http_get("https://qt.gtimg.cn/q=sh000001", proxy, 5, &len);

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
quote_engine_calc_market_clocks(time_t               now,
                                market_clock_item_t* out_clocks,
                                int                  max_clocks,
                                int*                 out_count)
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
        {"SSE_SZSE", "A股 (沪深)", "Asia/Shanghai",    8,  9, 30, 11, 30, 13, 0,  15, 0, 9, 15},
        {"HKEX",     "港股",       "Asia/Hong_Kong",   8,  9, 30, 12, 0,  13, 0,  16, 0, 9, 0 },
        {"TSE",      "东京",       "Asia/Tokyo",       9,  9, 0,  11, 30, 12, 30, 15, 0, 8, 30},
        {"LSE",      "伦敦",       "Europe/London",    1,  8, 0,  16, 30, 0,  0,  0,  0, 7, 0 },
        {"NYSE",     "美股",       "America/New_York", -4, 9, 30, 16, 0,  0,  0,  0,  0, 4, 0 },
    };

    *out_count = 5;
    for (int i = 0; i < 5; i++) {
        market_clock_item_t* c = &out_clocks[i];
        memset(c, 0, sizeof(*c));
        strncpy(c->market, configs[i].market, sizeof(c->market) - 1);
        strncpy(c->name, configs[i].name, sizeof(c->name) - 1);
        strncpy(c->timezone, configs[i].tz, sizeof(c->timezone) - 1);
        snprintf(c->open_time,
                 sizeof(c->open_time),
                 "%02d:%02d",
                 configs[i].open_h1,
                 configs[i].open_m1);
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

        int m_open1 = configs[i].open_h1 * 60 + configs[i].open_m1;
        int m_close1 = configs[i].close_h1 * 60 + configs[i].close_m1;
        int m_open2 = configs[i].open_h2 * 60 + configs[i].open_m2;
        int m_close2 = configs[i].close_h2 * 60 + configs[i].close_m2;
        int m_pre = configs[i].pre_h * 60 + configs[i].pre_m;

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

#define INDICES_CACHE_TTL_SEC 60
#define MAX_PRESET_INDICES 16

typedef struct {
    const char* symbol;
    const char* name;
    const char* category;
    const char* flag;
    const char* currency;
    const char* source;
    double      def_price;
    double      def_change;
} preset_index_t;

static const preset_index_t g_preset_indices[] = {
    {"sh000001", "上证指数", "cn",     "🇨🇳", "CNY", "stock_cn",  3285.40,  0.78 },
    {"sz399001", "深证成指", "cn",     "🇨🇳", "CNY", "stock_cn",  10550.20, 0.52 },
    {"sz399006", "创业板指", "cn",     "🇨🇳", "CNY", "stock_cn",  2188.60,  1.15 },
    {"sh000300", "沪深300",  "cn",     "🇨🇳", "CNY", "stock_cn",  3920.10,  0.65 },
    {"hkHSI",    "恒生指数", "hk",     "🇭🇰", "HKD", "stock_hk",  20630.00, -0.25},
    {"hkHSTECH", "恒生科技", "hk",     "🇭🇰", "HKD", "stock_hk",  4520.50,  0.42 },
    {"^GSPC",    "标普 500", "us",     "🇺🇸", "USD", "index",     5864.67,  0.42 },
    {"^IXIC",    "纳斯达克", "us",     "🇺🇸", "USD", "index",     18373.61, -0.18},
    {"^DJI",     "道琼斯",   "us",     "🇺🇸", "USD", "index",     43065.22, 0.35 },
    {"^N225",    "日经 225", "global", "🇯🇵", "JPY", "index",     39180.30, 0.88 },
    {"^GDAXI",   "德国 DAX", "global", "🇩🇪", "EUR", "index",     19580.40, 0.21 },
    {"^FTSE",    "英国富时", "global", "🇬🇧", "GBP", "index",     8240.10,  -0.05},
    {"BTC-USD",  "比特币",   "crypto", "🪙", "USD", "crypto",    67800.00, 2.45 },
    {"ETH-USD",  "以太坊",   "crypto", "🪙", "USD", "crypto",    2640.00,  1.82 },
    {"GC=F",     "黄金现货", "crypto", "🪙", "USD", "commodity", 2680.50,  0.38 },
};

static struct {
    pthread_mutex_t     lock;
    pthread_mutex_t     refresh_lock; /* 串行化刷新（HTTP）本身，避免并发刷新重复拉取 */
    time_t              last_updated;
    global_index_item_t items[MAX_PRESET_INDICES];
    int                 count;
    market_clock_item_t clocks[8];
    int                 clock_count;
    uint64_t            generation; /* 单调代数：刷新完成后自增，读路径据此判断是否需刷新 */
} g_indices_cache = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .refresh_lock = PTHREAD_MUTEX_INITIALIZER,
    .last_updated = 0,
    .count = 0,
    .clock_count = 0,
    .generation = 0,
};

static const char*
get_market_clock_id(const char* category, const char* symbol)
{
    if (strcmp(category, "cn") == 0) {
        return "SSE_SZSE";
    }
    if (strcmp(category, "hk") == 0) {
        return "HKEX";
    }
    if (strcmp(category, "us") == 0) {
        return "NYSE";
    }
    if (strcmp(symbol, "^N225") == 0) {
        return "TSE";
    }
    if (strcmp(symbol, "^GDAXI") == 0 || strcmp(symbol, "^FTSE") == 0) {
        return "LSE";
    }
    return NULL; /* crypto 7x24 */
}

static bool
is_market_active(const char* market_id, const market_clock_item_t* clocks, int clock_count)
{
    if (!market_id) {
        return true; /* crypto / continuous */
    }
    for (int i = 0; i < clock_count; i++) {
        if (strcmp(clocks[i].market, market_id) == 0) {
            return (strcmp(clocks[i].status, "open") == 0 || strcmp(clocks[i].status, "pre") == 0);
        }
    }
    return false;
}

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

static bool
indices_cache_is_stale_locked(time_t now)
{
    return (g_indices_cache.count == 0) ||
           ((now - g_indices_cache.last_updated) > INDICES_CACHE_TTL_SEC);
}

static const global_index_item_t*
find_cached_index_by_symbol(const global_index_item_t* items, int count, const char* symbol)
{
    if (!items || count <= 0 || !symbol) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        if (strcmp(items[i].symbol, symbol) == 0 && items[i].current_price > 0) {
            return &items[i];
        }
    }
    return NULL;
}

/* 计算并回填指数缓存：在锁外做 HTTP 拉取（避免阻塞读路径），
   全部 15 条指数拉取完成后一次性持锁原子替换缓存，刷新期间读路径继续服务完整旧数据（stale-while-revalidate）。
   并发调用方须以 refresh_lock 串行化本函数（见 refresh_indices_cache_offlock）。 */
static void
refresh_indices_cache_offlock_impl(void)
{
    time_t              now = time(NULL);
    market_clock_item_t local_clocks[8];
    int                 local_clock_count = 0;
    quote_engine_calc_market_clocks(now, local_clocks, 8, &local_clock_count);

    int                 n = (int)(sizeof(g_preset_indices) / sizeof(g_preset_indices[0]));
    global_index_item_t local_items[MAX_PRESET_INDICES];
    int                 local_count = 0;

    global_index_item_t prev_items[MAX_PRESET_INDICES];
    int                 prev_count = 0;
    pthread_mutex_lock(&g_indices_cache.lock);
    prev_count = g_indices_cache.count;
    if (prev_count > MAX_PRESET_INDICES) {
        prev_count = MAX_PRESET_INDICES;
    }
    if (prev_count > 0) {
        memcpy(prev_items, g_indices_cache.items, sizeof(global_index_item_t) * (size_t)prev_count);
    }
    pthread_mutex_unlock(&g_indices_cache.lock);

    int active_attempted = 0;
    int active_succeeded = 0;

    for (int i = 0; i < n && local_count < MAX_PRESET_INDICES; i++) {
        const char* mkt =
            get_market_clock_id(g_preset_indices[i].category, g_preset_indices[i].symbol);
        bool active = is_market_active(mkt, local_clocks, local_clock_count);
        if (active) {
            active_attempted++;
        }

        const global_index_item_t* prev_item =
            find_cached_index_by_symbol(prev_items, prev_count, g_preset_indices[i].symbol);

        /* 锁外做 HTTP 拉取，避免长时间持有读缓存锁阻塞读路径 */
        market_quote_t q;
        bool fetched = (active &&
                        quote_engine_fetch_quote(
                            g_preset_indices[i].symbol, g_preset_indices[i].source, &q) == 0 &&
                        q.current_price > 0);
        if (fetched) {
            active_succeeded++;
        } else if (prev_item) {
            local_items[local_count++] = *prev_item;
            continue;
        }

        global_index_item_t item;
        memset(&item, 0, sizeof(item));
        strncpy(item.symbol, g_preset_indices[i].symbol, sizeof(item.symbol) - 1);
        strncpy(item.name, g_preset_indices[i].name, sizeof(item.name) - 1);
        strncpy(item.category, g_preset_indices[i].category, sizeof(item.category) - 1);
        strncpy(item.region_flag, g_preset_indices[i].flag, sizeof(item.region_flag) - 1);
        strncpy(item.currency, g_preset_indices[i].currency, sizeof(item.currency) - 1);

        if (fetched) {
            item.current_price = q.current_price;
            item.change_percent = q.change_percent;
            snprintf(item.quote_time, sizeof(item.quote_time), "%s", q.quote_time);
        } else {
            item.current_price = g_preset_indices[i].def_price;
            item.change_percent = g_preset_indices[i].def_change;
            struct tm tm_q;
            localtime_r(&now, &tm_q);
            strftime(item.quote_time, sizeof(item.quote_time), "%Y-%m-%d %H:%M:%S", &tm_q);
        }

        item.change_amount = item.current_price * (item.change_percent / 100.0) /
                             (1.0 + item.change_percent / 100.0);
        item.prev_close = item.current_price - item.change_amount;
        item.open_price = item.prev_close * (1.0 + (item.change_percent * 0.3) / 100.0);
        item.high_price = item.current_price > item.open_price ? item.current_price * 1.004
                                                               : item.open_price * 1.003;
        item.low_price = item.current_price < item.open_price ? item.current_price * 0.996
                                                              : item.open_price * 0.997;

        item.sparkline_count = 8;
        populate_mock_sparkline(item.current_price, item.change_percent, item.sparkline, 8);

        local_items[local_count++] = item;
    }

    time_t finished_at = time(NULL);
    pthread_mutex_lock(&g_indices_cache.lock);
    if (prev_count > 0 && active_attempted > 0 && active_succeeded == 0) {
        memcpy(g_indices_cache.clocks,
               local_clocks,
               sizeof(market_clock_item_t) * (size_t)local_clock_count);
        g_indices_cache.clock_count = local_clock_count;
    } else {
        memcpy(
            g_indices_cache.items, local_items, sizeof(global_index_item_t) * (size_t)local_count);
        g_indices_cache.count = local_count;
        memcpy(g_indices_cache.clocks,
               local_clocks,
               sizeof(market_clock_item_t) * (size_t)local_clock_count);
        g_indices_cache.clock_count = local_clock_count;
        g_indices_cache.last_updated = finished_at;
        g_indices_cache.generation++;
    }
    pthread_mutex_unlock(&g_indices_cache.lock);
}

/* 须在持有 g_indices_cache.refresh_lock 时调用：双重检查缓存是否仍陈旧后再刷新。 */
static void
refresh_indices_cache_if_stale_locked(void)
{
    pthread_mutex_lock(&g_indices_cache.lock);
    bool still_stale = indices_cache_is_stale_locked(time(NULL));
    pthread_mutex_unlock(&g_indices_cache.lock);
    if (still_stale) {
        refresh_indices_cache_offlock_impl();
    }
}

/* 串行化入口：以 refresh_lock 保护 refresh_indices_cache_offlock_impl，
   避免多个线程重复拉取 HTTP。 */
static void
refresh_indices_cache_offlock(void)
{
    pthread_mutex_lock(&g_indices_cache.refresh_lock);
    refresh_indices_cache_offlock_impl();
    pthread_mutex_unlock(&g_indices_cache.refresh_lock);
}

int
quote_engine_sync_indices_to_db(void* pool)
{
    if (!pool) {
        return -1;
    }

    /* 串行化指数刷新（HTTP），避免并发同步任务重复拉取 */
    refresh_indices_cache_offlock();

    time_t              now = time(NULL);
    market_clock_item_t clocks[8];
    int                 clock_count = 0;
    quote_engine_calc_market_clocks(now, clocks, 8, &clock_count);

    int n = sizeof(g_preset_indices) / sizeof(g_preset_indices[0]);

    for (int i = 0; i < n; i++) {
        const char* mkt =
            get_market_clock_id(g_preset_indices[i].category, g_preset_indices[i].symbol);
        bool active = is_market_active(mkt, clocks, clock_count);
        if (!active) {
            scheduler_event_publish_market_closed(mkt);
        }

        global_index_item_t item;
        memset(&item, 0, sizeof(item));
        strncpy(item.symbol, g_preset_indices[i].symbol, sizeof(item.symbol) - 1);
        strncpy(item.name, g_preset_indices[i].name, sizeof(item.name) - 1);
        strncpy(item.category, g_preset_indices[i].category, sizeof(item.category) - 1);
        strncpy(item.region_flag, g_preset_indices[i].flag, sizeof(item.region_flag) - 1);
        strncpy(item.currency, g_preset_indices[i].currency, sizeof(item.currency) - 1);

        /* 非活跃时段：短持锁复用缓存（读路径在锁外回填），不再持锁做 HTTP */
        if (!active) {
            pthread_mutex_lock(&g_indices_cache.lock);
            bool found = false;
            for (int j = 0; j < g_indices_cache.count; j++) {
                if (strcmp(g_indices_cache.items[j].symbol, g_preset_indices[i].symbol) == 0 &&
                    g_indices_cache.items[j].current_price > 0) {
                    item = g_indices_cache.items[j];
                    found = true;
                    break;
                }
            }
            pthread_mutex_unlock(&g_indices_cache.lock);
            if (found) {
                char sp_json[256];
                snprintf(sp_json,
                         sizeof(sp_json),
                         "[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]",
                         item.sparkline[0],
                         item.sparkline[1],
                         item.sparkline[2],
                         item.sparkline[3],
                         item.sparkline[4],
                         item.sparkline[5],
                         item.sparkline[6],
                         item.sparkline[7]);
                mf_market_repo_upsert_index(pool,
                                            item.symbol,
                                            item.name,
                                            item.category,
                                            item.region_flag,
                                            item.current_price,
                                            item.change_amount,
                                            item.change_percent,
                                            item.prev_close,
                                            item.open_price,
                                            item.high_price,
                                            item.low_price,
                                            item.currency,
                                            item.quote_time,
                                            sp_json);
                continue;
            }
        }

        market_quote_t q;
        if (quote_engine_fetch_quote(g_preset_indices[i].symbol, g_preset_indices[i].source, &q) ==
                0 &&
            q.current_price > 0) {
            item.current_price = q.current_price;
            item.change_percent = q.change_percent;
            snprintf(item.quote_time, sizeof(item.quote_time), "%s", q.quote_time);
        } else {
            item.current_price = g_preset_indices[i].def_price;
            item.change_percent = g_preset_indices[i].def_change;
            struct tm tm_q;
            localtime_r(&now, &tm_q);
            strftime(item.quote_time, sizeof(item.quote_time), "%Y-%m-%d %H:%M:%S", &tm_q);
        }

        item.change_amount = item.current_price * (item.change_percent / 100.0) /
                             (1.0 + item.change_percent / 100.0);
        item.prev_close = item.current_price - item.change_amount;
        item.open_price = item.prev_close * (1.0 + (item.change_percent * 0.3) / 100.0);
        item.high_price = item.current_price > item.open_price ? item.current_price * 1.004
                                                               : item.open_price * 1.003;
        item.low_price = item.current_price < item.open_price ? item.current_price * 0.996
                                                              : item.open_price * 0.997;
        item.sparkline_count = 8;
        populate_mock_sparkline(item.current_price, item.change_percent, item.sparkline, 8);

        char sp_json[256];
        snprintf(sp_json,
                 sizeof(sp_json),
                 "[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]",
                 item.sparkline[0],
                 item.sparkline[1],
                 item.sparkline[2],
                 item.sparkline[3],
                 item.sparkline[4],
                 item.sparkline[5],
                 item.sparkline[6],
                 item.sparkline[7]);

        mf_market_repo_upsert_index(pool,
                                    item.symbol,
                                    item.name,
                                    item.category,
                                    item.region_flag,
                                    item.current_price,
                                    item.change_amount,
                                    item.change_percent,
                                    item.prev_close,
                                    item.open_price,
                                    item.high_price,
                                    item.low_price,
                                    item.currency,
                                    item.quote_time,
                                    sp_json);
    }

    return 0;
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

    /* 读路径：陈旧时才触发刷新，且刷新在锁外进行（stale-while-revalidate），
       持锁仅做数据拷贝，不再阻塞于 HTTP。 */
    pthread_mutex_lock(&g_indices_cache.lock);
    time_t now = time(NULL);
    bool   has_cached = (g_indices_cache.count > 0);
    bool   stale = indices_cache_is_stale_locked(now);
    pthread_mutex_unlock(&g_indices_cache.lock);

    if (stale) {
        if (has_cached) {
            if (pthread_mutex_trylock(&g_indices_cache.refresh_lock) == 0) {
                refresh_indices_cache_if_stale_locked();
                pthread_mutex_unlock(&g_indices_cache.refresh_lock);
            }
        } else {
            pthread_mutex_lock(&g_indices_cache.refresh_lock);
            refresh_indices_cache_if_stale_locked();
            pthread_mutex_unlock(&g_indices_cache.refresh_lock);
        }
    }

    pthread_mutex_lock(&g_indices_cache.lock);

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
