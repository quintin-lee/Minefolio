#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include "common/db.h"
#include "common/market_types.h"
#include "config/secret.h"
#include "services/market/quote_engine.h"
#include "services/market/quote_driver.h"
#include "infrastructure/repositories/market_repo_impl.h"

static void test_market_clocks_calc(void) {
    market_clock_item_t clocks[8];
    int count = 0;

    /* 设定一个模拟时间: 2026-10-16 10:00:00 UTC (北京时间 18:00 周五已收盘, 伦敦时间 11:00 周五交易中) */
    struct tm tm_test = {0};
    tm_test.tm_year = 2026 - 1900;
    tm_test.tm_mon = 9; /* 10月 */
    tm_test.tm_mday = 16;
    tm_test.tm_hour = 10;
    tm_test.tm_min = 0;
    tm_test.tm_sec = 0;
    time_t mock_now = timegm(&tm_test);

    int rc = quote_engine_calc_market_clocks(mock_now, clocks, 8, &count);
    assert(rc == 0);
    assert(count == 5);

    /* 验证 A 股在 18:00 为已收盘 */
    assert(strcmp(clocks[0].market, "SSE_SZSE") == 0);
    assert(strcmp(clocks[0].status, "closed") == 0);

    /* 验证伦敦在 11:00 (UTC 10:00, BST 11:00) 为交易中 open */
    assert(strcmp(clocks[3].market, "LSE") == 0);
    assert(strcmp(clocks[3].status, "open") == 0);

    printf("PASS: test_market_clocks_calc\n");
}

static void test_quote_engine_get_indices_cache(void) {
    global_index_item_t indices[32];
    int count = 0;
    market_clock_item_t clocks[8];
    int clock_count = 0;

    /* 第一次获取 */
    int rc = quote_engine_get_indices("all", indices, 32, &count, clocks, 8, &clock_count);
    assert(rc == 0);
    assert(count >= 10);
    assert(clock_count == 5);

    char first_quote_time[32];
    strncpy(first_quote_time, indices[0].quote_time, sizeof(first_quote_time));

    /* 立即第二次获取: 应命中 20s 内存缓存 */
    global_index_item_t indices2[32];
    int count2 = 0;
    rc = quote_engine_get_indices("cn", indices2, 32, &count2, clocks, 8, &clock_count);
    assert(rc == 0);
    assert(count2 == 4); /* 上证, 深证, 创业板, 沪深300 */
    assert(strcmp(indices2[0].category, "cn") == 0);

    printf("PASS: test_quote_engine_get_indices_cache\n");
}

#include "application/market/usecases.h"

static void test_market_usecase_indices(void) {
    csilk_json_t* data = NULL;
    market_usecase_result_t res = {0};

    int rc = market_usecase_get_indices(NULL, "cn", &data, &res);
    assert(rc == 0);

    assert(res.code == 0);
    assert(data != NULL);

    const csilk_json_t* clocks = csilk_json_get_array(data, "market_clocks");
    assert(clocks != NULL && csilk_json_is_array(clocks));
    assert(csilk_json_array_size(clocks) == 5);

    const csilk_json_t* indices = csilk_json_get_array(data, "indices");
    assert(indices != NULL && csilk_json_is_array(indices));
    assert(csilk_json_array_size(indices) == 4);

    csilk_json_free(data);
    printf("PASS: test_market_usecase_indices\n");
}

static atomic_bool g_swr_stop = false;
static atomic_int  g_swr_bad_counts = 0;

static void*
swr_reader_thread(void* arg)
{
    (void)arg;
    while (!atomic_load(&g_swr_stop)) {
        global_index_item_t indices[32];
        int                 count = 0;
        market_clock_item_t clocks[8];
        int                 clock_count = 0;
        int rc = quote_engine_get_indices("all", indices, 32, &count, clocks, 8, &clock_count);
        if (rc != 0 || count != 15) {
            atomic_fetch_add(&g_swr_bad_counts, 1);
        }
    }
    return NULL;
}

static void
test_quote_engine_swr_concurrent_and_failed_refresh(void)
{
    const char* test_db = "/tmp/test_mf_market_indices_swr.db";
    unlink(test_db);
    char shm[256], wal[256];
    snprintf(shm, sizeof(shm), "%s-shm", test_db);
    snprintf(wal, sizeof(wal), "%s-wal", test_db);
    unlink(shm);
    unlink(wal);

    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", test_db);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_market_indices_32b!");

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    /* Ensure cache is populated first */
    global_index_item_t init_indices[32];
    int                 init_count = 0;
    assert(quote_engine_get_indices("all", init_indices, 32, &init_count, NULL, 0, NULL) == 0);
    assert(init_count == 15);

    /* 1. Concurrent reads during in-progress refresh must never see 0 or partial count */
    atomic_store(&g_swr_stop, false);
    atomic_store(&g_swr_bad_counts, 0);
    pthread_t readers[4];
    for (int t = 0; t < 4; t++) {
        assert(pthread_create(&readers[t], NULL, swr_reader_thread, NULL) == 0);
    }
    for (int iter = 0; iter < 30; iter++) {
        assert(quote_engine_sync_indices_to_db(pool) == 0);
    }
    atomic_store(&g_swr_stop, true);
    for (int t = 0; t < 4; t++) {
        pthread_join(readers[t], NULL);
    }
    assert(atomic_load(&g_swr_bad_counts) == 0);

    /* 2. Failed refresh must preserve stale cached price (100.0 from mock), not overwrite with def_price */
    global_index_item_t before[8];
    int                 bcount = 0;
    assert(quote_engine_get_indices("crypto", before, 8, &bcount, NULL, 0, NULL) == 0);
    assert(bcount == 3);
    double btc_before = 0.0;
    for (int i = 0; i < bcount; i++) {
        if (strcmp(before[i].symbol, "BTC-USD") == 0) {
            btc_before = before[i].current_price;
        }
    }
    assert(fabs(btc_before - 100.0) < 1e-4);

    quote_engine_request_stop();
    assert(quote_engine_sync_indices_to_db(pool) == 0);
    quote_engine_reset_stop();

    global_index_item_t after[8];
    int                 acount = 0;
    assert(quote_engine_get_indices("crypto", after, 8, &acount, NULL, 0, NULL) == 0);
    assert(acount == 3);
    double btc_after = 0.0;
    for (int i = 0; i < acount; i++) {
        if (strcmp(after[i].symbol, "BTC-USD") == 0) {
            btc_after = after[i].current_price;
        }
    }
    assert(fabs(btc_after - 100.0) < 1e-4);

    unlink(test_db);
    unlink(shm);
    unlink(wal);
    printf("PASS: test_quote_engine_swr_concurrent_and_failed_refresh\n");
}

static void
test_tencent_parse_empty_fields_and_ohlc(void)
{
    /* A股指数实测报文：第29字段为空（~~），旧 strtok_r 实现会左移1位导致涨跌幅变成最高价 */
    const char* sh_line =
        "v_sh000001=\"1~上证指数~000001~3606.49~3476.88~3482.38~634626966~0~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~~20261127161427~129.61~3.73~3610.72~3467.41~3606.49/634626966/929777242144\";";
    market_quote_t q;
    assert(driver_tencent_parse_line(sh_line, &q) == 0);
    assert(fabs(q.current_price - 3606.49) < 1e-4);
    assert(fabs(q.prev_close - 3476.88) < 1e-4);
    assert(fabs(q.open_price - 3482.38) < 1e-4);
    assert(fabs(q.change_amount - 129.61) < 1e-4);
    assert(fabs(q.change_percent - 3.73) < 1e-4);
    assert(fabs(q.high_price - 3610.72) < 1e-4);
    assert(fabs(q.low_price - 3467.41) < 1e-4);
    assert(strcmp(q.quote_time, "2026-11-27 16:14:27") == 0);
    assert(strcmp(q.currency, "CNY") == 0);

    const char* hk_line =
        "v_hkHSI=\"100~恒生指数~HSI~22694.770~22226.880~22263.700~25714049.4366~0~0~22694.770~0~0~0~0~0~0~0~0~0~22694.770~0~0~0~0~0~0~0~0~0~0.0~2026/11/27 18:30:57~467.890~2.11~22781.030~22263.700\";";
    assert(driver_tencent_parse_line(hk_line, &q) == 0);
    assert(fabs(q.current_price - 22694.770) < 1e-4);
    assert(fabs(q.change_amount - 467.890) < 1e-4);
    assert(fabs(q.change_percent - 2.11) < 1e-4);
    assert(fabs(q.high_price - 22781.030) < 1e-4);
    assert(fabs(q.low_price - 22263.700) < 1e-4);
    assert(strcmp(q.quote_time, "2026-11-27 18:30:57") == 0);
    assert(strcmp(q.currency, "HKD") == 0);

    const char* jj_line =
        "v_jj110011=\"110011~易方达精选~0.0000~0.0000~~4.2064~5.9964~-0.1638~2026-08-27~\";";
    assert(driver_tencent_parse_line(jj_line, &q) == 0);
    assert(fabs(q.current_price - 4.2064) < 1e-4);
    assert(fabs(q.change_percent - (-0.1638)) < 1e-4);
    assert(strcmp(q.quote_time, "2026-08-27") == 0);

    printf("PASS: test_tencent_parse_empty_fields_and_ohlc\n");
}

static void
test_crypto_symbol_normalization(void)
{
    char pair[64];
    driver_crypto_normalize_pair("BTC-USD", pair, sizeof(pair));
    assert(strcmp(pair, "BTCUSDT") == 0);
    driver_crypto_normalize_pair("eth-usd", pair, sizeof(pair));
    assert(strcmp(pair, "ETHUSDT") == 0);
    driver_crypto_normalize_pair("btc", pair, sizeof(pair));
    assert(strcmp(pair, "BTCUSDT") == 0);
    driver_crypto_normalize_pair("SOLUSDT", pair, sizeof(pair));
    assert(strcmp(pair, "SOLUSDT") == 0);
    printf("PASS: test_crypto_symbol_normalization\n");
}

static void
test_yahoo_encode_and_parse(void)
{
    char enc[64];
    driver_yahoo_encode_symbol("^GSPC", enc, sizeof(enc));
    assert(strcmp(enc, "%5EGSPC") == 0);
    driver_yahoo_encode_symbol("GC=F", enc, sizeof(enc));
    assert(strcmp(enc, "GC%3DF") == 0);

    const char* json =
        "{\"chart\":{\"result\":[{\"meta\":{\"currency\":\"USD\",\"symbol\":\"BTC-USD\","
        "\"regularMarketPrice\":59660.08,\"chartPreviousClose\":59866.066,"
        "\"regularMarketChangePercent\":-3.297,\"regularMarketDayHigh\":60164.19,"
        "\"regularMarketDayLow\":59672.88,\"regularMarketTime\":1795844705},"
        "\"indicators\":{\"quote\":[{\"open\":[61500.0],\"close\":[61000.0,60200.0,59660.08]}]}}]}}";
    market_quote_t q;
    assert(driver_yahoo_parse_chart_json(json, "BTC-USD", &q) == 0);
    assert(fabs(q.current_price - 59660.08) < 1e-4);
    assert(fabs(q.change_percent - (-3.297)) < 1e-4);
    assert(fabs(q.open_price - 61500.0) < 1e-4);
    assert(fabs(q.high_price - 60164.19) < 1e-4);
    assert(fabs(q.low_price - 59672.88) < 1e-4);
    assert(q.sparkline_count == 3);
    assert(fabs(q.sparkline[0] - 61000.0) < 1e-4);
    assert(fabs(q.sparkline[2] - 59660.08) < 1e-4);
    assert(q.quote_time[0] != '\0');
    printf("PASS: test_yahoo_encode_and_parse\n");
}

static void
test_market_clocks_dst_winter(void)
{
    market_clock_item_t clocks[8];
    int                 count = 0;

    /* 2026-11-27 14:00:00 UTC (周五冬令时: 伦敦 GMT 14:00=open, 纽约 EST 09:00=pre) */
    struct tm tm_w = {0};
    tm_w.tm_year = 2026 - 1900;
    tm_w.tm_mon = 10;
    tm_w.tm_mday = 27;
    tm_w.tm_hour = 14;
    time_t winter_14 = timegm(&tm_w);
    assert(quote_engine_calc_market_clocks(winter_14, clocks, 8, &count) == 0);
    assert(strcmp(clocks[3].market, "LSE") == 0);
    assert(strcmp(clocks[3].status, "open") == 0);
    assert(strcmp(clocks[4].market, "NYSE") == 0);
    assert(strcmp(clocks[4].status, "pre") == 0);

    /* 15:00 UTC -> 纽约 EST 10:00=open */
    assert(quote_engine_calc_market_clocks(winter_14 + 3600, clocks, 8, &count) == 0);
    assert(strcmp(clocks[4].status, "open") == 0);
    printf("PASS: test_market_clocks_dst_winter\n");
}

static void
test_failed_sync_never_overwrites_db_with_def_price(void)
{
    const char* test_db = "/tmp/test_mf_market_indices_nolive.db";
    unlink(test_db);
    char shm[256], wal[256];
    snprintf(shm, sizeof(shm), "%s-shm", test_db);
    snprintf(wal, sizeof(wal), "%s-wal", test_db);
    unlink(shm);
    unlink(wal);

    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", test_db);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_market_indices_32b!");

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    assert(mf_market_repo_upsert_index(pool,
                                       "sh000001",
                                       "上证指数",
                                       "cn",
                                       "🇨🇳",
                                       3606.49,
                                       129.61,
                                       3.73,
                                       3476.88,
                                       3482.38,
                                       3610.72,
                                       3467.41,
                                       "CNY",
                                       "2026-11-27 16:14:27",
                                       "[3476.88,3482.38,3606.49]") == 0);

    /* 模拟无网络失败时强行同步：绝不应拿硬编码 3285.40 覆盖 DB */
    quote_engine_request_stop();
    assert(quote_engine_force_sync_indices_to_db(pool) == 0);
    quote_engine_reset_stop();

    csilk_json_t* rows = mf_market_repo_get_indices(pool, "cn");
    assert(rows != NULL && csilk_json_is_array(rows));
    double sh_price = 0.0;
    for (size_t i = 0; i < csilk_json_array_size(rows); i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        const char*   sym = csilk_json_get_string(r, "symbol");
        if (sym && strcmp(sym, "sh000001") == 0) {
            sh_price = db_get_num(r, "current_price");
        }
    }
    csilk_json_free(rows);
    assert(fabs(sh_price - 3606.49) < 1e-4);

    unlink(test_db);
    unlink(shm);
    unlink(wal);
    printf("PASS: test_failed_sync_never_overwrites_db_with_def_price\n");
}

int main(void) {
    setenv("MINEFOLIO_MARKET_MOCK", "1", 1);
    printf("=== Running Unit Test: Market Indices & Clocks ===\n");
    test_tencent_parse_empty_fields_and_ohlc();
    test_crypto_symbol_normalization();
    test_yahoo_encode_and_parse();
    test_market_clocks_calc();
    test_market_clocks_dst_winter();
    test_failed_sync_never_overwrites_db_with_def_price();
    test_quote_engine_get_indices_cache();
    test_market_usecase_indices();
    test_quote_engine_swr_concurrent_and_failed_refresh();
    return 0;
}
