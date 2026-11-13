/**
 * @file performance.c
 * @brief 区间收益率 / 当日涨跌 纯领域实现
 *
 * 严格纯 C，仅依赖 <string.h> <time.h> <stdlib.h> + core/financial。
 * 区间端点采用"目标日 ≤ 的最近一行"近似交易日历（无严格日历对齐）。
 */
#include "domain/asset/performance.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>

/** 解析 "YYYY-MM-DD" 为 time_t（本地时区；解析失败返回 -1） */
static time_t
parse_date_to_time(const char* date_str)
{
    if (!date_str || strlen(date_str) < 10) {
        return -1;
    }
    int y = (date_str[0] - '0') * 1000 + (date_str[1] - '0') * 100 + (date_str[2] - '0') * 10 +
            (date_str[3] - '0');
    int       m = (date_str[5] - '0') * 10 + (date_str[6] - '0');
    int       d = (date_str[8] - '0') * 10 + (date_str[9] - '0');
    struct tm tm = {0};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

/** 在升序序列中找 price_date <= cutoff 的最近一行下标；未找到返回 -1 */
static long
find_last_index_leq(const mf_price_point_t* series, size_t n, time_t cutoff)
{
    for (size_t i = n; i > 0; i--) {
        time_t t = parse_date_to_time(series[i - 1].date);
        if (t >= 0 && t <= cutoff) {
            return (long)(i - 1);
        }
    }
    return -1;
}

/** 计算单点区间收益率：end_idx 与 start_idx 之间；返回百分比 double，失败 -1.0 */
static double
compute_interval_return_pct(const mf_price_point_t* series, size_t n, long start_idx, long end_idx)
{
    if (start_idx < 0 || end_idx < 0 || start_idx >= (long)n || end_idx >= (long)n) {
        return -1.0;
    }
    price_t start_price = series[start_idx].price;
    price_t end_price = series[end_idx].price;
    if (price_cmp(start_price, price_zero(start_price.currency)) <= 0) {
        return -1.0; /* 起点价为 0 无法计算比率 */
    }
    /* 用 money 类型做差：(end - start) / start * 100% */
    quantity_t one;
    quantity_from_double(1.0, 0, &one);
    money_t diff;
    if (price_times_quantity(end_price, one, &diff) != DECIMAL_OK) {
        return -1.0;
    }
    money_t start_m;
    if (price_times_quantity(start_price, one, &start_m) != DECIMAL_OK) {
        return -1.0;
    }
    money_t delta;
    if (money_sub(diff, start_m, &delta) != DECIMAL_OK) {
        return -1.0;
    }
    percentage_t pct;
    if (money_is_positive(start_m) &&
        percentage_calc(delta, start_m, 4, ROUND_HALF_UP, &pct) == DECIMAL_OK) {
        return percentage_to_double(pct);
    }
    return 0.0;
}

int
mf_interval_returns_from_series(const mf_price_point_t* series,
                                size_t                  n,
                                const char*             target_date_str,
                                mf_interval_returns_t*  out)
{
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (n == 0 || !series) {
        return -1; /* 无数据 */
    }
    if (!target_date_str || strlen(target_date_str) < 10) {
        return -1;
    }

    time_t target_t = parse_date_to_time(target_date_str);
    if (target_t < 0) {
        return -1;
    }

    /* 端点：最新价（target_date 前的最后一行；通常即序列末尾） */
    long end_idx = find_last_index_leq(series, n, target_t);
    if (end_idx < 0) {
        /* 所有价格都晚于 target_date，退化为"全历史"模式（end=序列末行） */
        end_idx = (long)n - 1;
    }

    /* ret_all：起点 = 序列首行；终点 = end_idx */
    out->ret_all = compute_interval_return_pct(series, n, 0, end_idx);
    out->valid_all = (out->ret_all >= 0.0) && (n >= 1 && series[0].date[0] != '\0');
    if (out->valid_all && out->ret_all < 0.0) {
        out->ret_all = 0.0;
    }

    /* 固定区间 1M/3M/6M/1Y：起点 = target_date 往前推 N 天 */
    const long intervals[] = {30L, 90L, 180L, 365L};
    double*    rets[] = {&out->ret_1m, &out->ret_3m, &out->ret_6m, &out->ret_1y};
    bool*      valids[] = {&out->valid_1m, &out->valid_3m, &out->valid_6m, &out->valid_1y};

    for (int k = 0; k < 4; k++) {
        time_t start_t = target_t - intervals[k] * 86400;
        long   start_idx = find_last_index_leq(series, n, start_t);
        if (start_idx < 0) {
            start_idx = 0;
        }
        double ret = compute_interval_return_pct(series, n, start_idx, end_idx);
        *rets[k] = (ret >= 0.0) ? ret : 0.0;
        *valids[k] = (ret >= 0.0);
        if (start_idx == 0 && parse_date_to_time(series[0].date) > start_t) {
            *valids[k] = false;
        }
    }

    return 0;
}

int
mf_daily_change_from_series(const mf_price_point_t* series,
                            size_t                  n,
                            money_t*                out_change,
                            double*                 out_pct)
{
    if (!out_change || !out_pct || !series || n < 2) {
        return -1;
    }
    *out_change = money_zero(series[n - 1].price.currency);
    *out_pct = 0.0;

    price_t latest = series[n - 1].price;
    price_t prev = series[n - 2].price;
    if (price_cmp(prev, price_zero(prev.currency)) <= 0) {
        return -2; /* 前一日价为 0，无法计算 */
    }

    /* change = latest - prev（金额） */
    quantity_t one;
    quantity_from_double(1.0, 0, &one);
    money_t latest_m, prev_m;
    if (price_times_quantity(latest, one, &latest_m) != DECIMAL_OK ||
        price_times_quantity(prev, one, &prev_m) != DECIMAL_OK) {
        return -1;
    }
    money_sub(latest_m, prev_m, out_change);
    *out_change = *out_change; /* 规范化币种 */

    /* pct = (latest - prev) / prev * 100% */
    percentage_t pct;
    if (money_is_positive(prev_m) &&
        percentage_calc(*out_change, prev_m, 4, ROUND_HALF_UP, &pct) == DECIMAL_OK) {
        *out_pct = percentage_to_double(pct);
    }
    return 0;
}
