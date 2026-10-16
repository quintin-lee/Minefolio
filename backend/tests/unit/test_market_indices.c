#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "common/market_types.h"
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

int main(void) {
    setenv("MINEFOLIO_MARKET_MOCK", "1", 1);
    printf("=== Running Unit Test: Market Indices & Clocks ===\n");
    test_market_clocks_calc();
    test_quote_engine_get_indices_cache();
    test_market_usecase_indices();
    return 0;
}
