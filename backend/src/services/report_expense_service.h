#pragma once
#include "application/report/commands.h"

/**
 * @brief 月度收支报表（入参 pool/查询参数，结果通过 out_res 承载）
 */
void report_expense_monthly(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 收支趋势报表
 */
void report_expense_trend(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 年度收支报表
 */
void report_expense_yearly(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 分类支出分布
 */
void report_expense_category(void* pool, const report_query_t* q, report_result_t* out_res);

/**
 * @brief 标签支出统计
 */
void report_expense_tag(void* pool, const report_query_t* q, report_result_t* out_res);
