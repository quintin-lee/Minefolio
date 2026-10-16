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

int main(void) {
    printf("=== Running Unit Test: Market Indices & Clocks ===\n");
    test_market_clocks_calc();
    return 0;
}
