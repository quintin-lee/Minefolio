/**
 * @file daily_expense_repo_impl.h
 * @brief 日常收支仓储 SQL 实现声明 (Infrastructure Daily Expense Repository)
 */

#pragma once

#include "domain/daily_expense/repository.h"

/**
 * @brief 解析日常收支查询原始行中的 tags 字符串并格式化为标准 JSON 数组
 * @param[in] raw_rows 包含 tags 字符串字段的收支原始行数组 (函数接管其生命周期并释放)
 * @return 带有标准 JSON 数组 tags 的新数组
 */
csilk_json_t* mf_format_expense_rows_with_tags(csilk_json_t* raw_rows);
