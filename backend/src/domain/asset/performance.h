#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "core/financial/price.h"
#include "core/financial/percentage.h"

/**
 * @file performance.h
 * @brief 区间收益率 / 当日涨跌 纯领域计算模型 (Domain Performance Metrics)
 *
 * 严格禁止依赖任何外部存储 (DB/HTTP/JSON)。
 * 输入：标的的日频价格序列（`mf_price_point_t`）；
 * 输出：各固定区间 (1M/3M/6M/1Y/ALL) 的区间收益率（百分比）。
 *
 * 设计约束：
 * - 序列按 price_date 升序排列（调用方负责排序）；
 * - 区间端点取"目标日 ≤ 的最近一行"价格（近似交易日历，无需严格对齐）；
 * - 数据不足 (序列端点缺失 / 区间起点价为 0) 时，对应区间 `valid` 标记 false；
 * - 区间收益率用 `percentage_calc` 在 128-bit 固定点上计算，杜绝 double 漂移。
 */

/**
 * @brief 单个价格采样点 (日期 + 单价)
 * @note date 格式为 "YYYY-MM-DD"，price 为该日的单位净值 / 成交价
 */
typedef struct {
    char    date[16]; /**< 采样日期，升序 */
    price_t price;    /**< 该日单位净值 */
} mf_price_point_t;

/**
 * @brief 固定区间收益率结果
 * @note 各字段为百分比 (如 5.23 表示 +5.23%)；valid=false 表示数据不足无法计算
 */
typedef struct {
    double ret_1m;
    double ret_3m;
    double ret_6m;
    double ret_1y;
    double ret_all;
    bool   valid_1m;
    bool   valid_3m;
    bool   valid_6m;
    bool   valid_1y;
    bool   valid_all;
} mf_interval_returns_t;

/**
 * @brief 计算各固定区间的收益率
 *
 * @param series   日频价格序列 (升序)
 * @param n        序列长度
 * @param target_date_str  目标基准日 (YYYY-MM-DD，通常为"今日")，用于端点定位
 * @param[out] out 区间收益率结果 (调用前需 memset 0)
 * @return 0 成功；-1 参数非法 (series==NULL && n>0 或 target_date_str 为空)
 *
 * 计算规则：
 * - ret_1m/3m/6m/1y 的区间起点 = target_date 往前推 30/90/180/365 天；
 * - 区间起点价 = 序列中 price_date ≤ 起点日 的最近一行（缺失则 valid=false）；
 * - 区间终点价 = 序列中 price_date ≤ target_date 的最近一行（通常即最新价）；
 * - ret = (end - start) / start * 100%；
 * - ret_all 的区间起点 = 序列首行（最早可查价的成本锚点），终点 = 最新价。
 */
int
mf_interval_returns_from_series(
    const mf_price_point_t* series, size_t n, const char* target_date_str, mf_interval_returns_t* out);

/**
 * @brief 计算"当日涨跌"（最新价相对前一日价）
 *
 * @param series  日频价格序列 (升序)
 * @param n       序列长度
 * @param[out] out_change  当日涨跌额（绝对值，最新价 - 前一日价）
 * @param[out] out_pct     当日涨跌幅（百分比，如 1.23 表示 +1.23%）
 * @return 0 成功；-1 参数非法；-2 数据不足（序列 < 2 行，无法对比昨日）
 *
 * 约定：序列最后一行为最新价，倒数第二行为"前一日"价。
 * 涨跌额 / 涨跌幅均继承序列的计价币种；前一日价为 0 时 pct 置 0。
 */
int
mf_daily_change_from_series(const mf_price_point_t* series,
                            size_t                   n,
                            money_t*                 out_change,
                            double*                  out_pct);
