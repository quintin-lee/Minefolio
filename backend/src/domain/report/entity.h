/**
 * @file entity.h
 * @brief 报表领域实体定义 (Domain Report Entity)
 *
 * 报表域为只读查询域，实体主要为查询结果的数据结构。
 */

#pragma once

#include <stdint.h>

/**
 * @brief 月度收支汇总
 */
typedef struct {
    double total_income;
    double total_expense;
    double balance;
} mf_report_monthly_totals_t;

/**
 * @brief 趋势数据点
 */
typedef struct {
    char   period[16]; /**< 周期标签 (如 "2026-09") */
    double income;
    double expense;
} mf_report_trend_point_t;

/**
 * @brief 分类/标签分布项
 */
typedef struct {
    char    name[128];
    double  amount;
    double  pct;
    int64_t count; /**< 仅标签分布使用 */
} mf_report_distribution_item_t;
