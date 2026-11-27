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

int main(void) {
    setenv("MINEFOLIO_MARKET_MOCK", "1", 1);
    printf("=== Running Unit Test: Market Indices & Clocks ===\n");
    test_market_clocks_calc();
    test_quote_engine_get_indices_cache();
    test_market_usecase_indices();
    test_quote_engine_swr_concurrent_and_failed_refresh();
    return 0;
}
