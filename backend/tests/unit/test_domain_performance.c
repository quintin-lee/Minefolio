#include "domain/asset/performance.h"
#include "core/financial/currency.h"
#include "core/financial/price.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static mf_price_point_t make_point(const char* date, double price) {
    mf_price_point_t p;
    memset(&p, 0, sizeof(p));
    snprintf(p.date, sizeof(p.date), "%s", date);
    price_from_double(price, 4, CURRENCY_CNY, &p.price);
    return p;
}

void test_interval_returns_basic() {
    /* 构建 400 天的价格序列：从 100 线性增长到 200 */
    const int N = 400;
    mf_price_point_t series[N];
    for (int i = 0; i < N; i++) {
        /* 2025-01-01 起第 i 天 */
        int day = i + 1;
        int m = (day - 1) / 28 + 1;
        int d = (day - 1) % 28 + 1;
        char date[16];
        snprintf(date, sizeof(date), "2025-%02d-%02d", m, d);
        double price = 100.0 + (100.0 / (N - 1)) * i; /* 100 → 200 */
        series[i] = make_point(date, price);
    }

    mf_interval_returns_t rets;
    memset(&rets, 0, sizeof(rets));
    int rc = mf_interval_returns_from_series(series, N, "2026-01-10", &rets);
    assert(rc == 0);

    /* ALL：从首行(100)到末行(~200)，应显著正收益（>50%） */
    assert(rets.valid_all);
    assert(rets.ret_all > 50.0);

    /* 1M：30天前价格 < 末行价格（单调递增序列）→ 正收益 */
    assert(rets.valid_1m);
    assert(rets.ret_1m > 0.0);

    /* 3M / 6M / 1Y 均有效（400天足够覆盖 365天） */
    assert(rets.valid_3m);
    assert(rets.valid_6m);
    assert(rets.valid_1y);
    assert(rets.ret_1y > rets.ret_6m); /* 更长期收益更高（单调递增序列） */

    printf("PASS: test_interval_returns_basic (all=%.2f%% 1m=%.2f%% 1y=%.2f%%)\n",
           rets.ret_all, rets.ret_1m, rets.ret_1y);
}

void test_interval_returns_insufficient_data() {
    /* 只有 10 天数据，1M/3M/6M/1Y 起点会退化为序列首行 → valid=false */
    const int N = 10;
    mf_price_point_t series[N];
    for (int i = 0; i < N; i++) {
        series[i] = make_point("2026-11-01", 100.0 + i);
    }

    mf_interval_returns_t rets;
    memset(&rets, 0, sizeof(rets));
    int rc = mf_interval_returns_from_series(series, N, "2026-11-10", &rets);
    assert(rc == 0);

    /* 1Y 起点 2025-11-10，序列最早 2026-11-01 > 起点 → valid_1y 应为 false */
    assert(!rets.valid_1y);
    printf("PASS: test_interval_returns_insufficient_data\n");
}

void test_interval_returns_empty_series() {
    mf_interval_returns_t rets;
    memset(&rets, 0, sizeof(rets));
    int rc = mf_interval_returns_from_series(NULL, 0, "2026-11-10", &rets);
    assert(rc == -1);
    printf("PASS: test_interval_returns_empty_series\n");
}

void test_daily_change_basic() {
    const int N = 5;
    mf_price_point_t series[N];
    for (int i = 0; i < N; i++) {
        series[i] = make_point("2026-11-05", 100.0 + i);
    }

    money_t change;
    double pct = 0.0;
    int rc = mf_daily_change_from_series(series, N, &change, &pct);
    assert(rc == 0);
    /* latest=104, prev=103 → change=1.0, pct≈0.97% */
    assert(fabs(money_to_double(change) - 1.0) < 0.01);
    assert(fabs(pct - 0.97) < 0.05);
    printf("PASS: test_daily_change_basic (change=%.2f pct=%.3f%%)\n",
           money_to_double(change), pct);
}

void test_daily_change_insufficient() {
    const int N = 1;
    mf_price_point_t series[N];
    series[0] = make_point("2026-11-05", 100.0);

    money_t change;
    double pct = 0.0;
    int rc = mf_daily_change_from_series(series, N, &change, &pct);
    assert(rc == -1); /* n < 2 */
    printf("PASS: test_daily_change_insufficient\n");
}

int main() {
    test_interval_returns_basic();
    test_interval_returns_insufficient_data();
    test_interval_returns_empty_series();
    test_daily_change_basic();
    test_daily_change_insufficient();
    printf("All domain performance tests passed!\n");
    return 0;
}
