/**
 * @file rules.h
 * @brief 日常收支业务规则声明 (Domain Daily Expense Rules)
 */

#pragma once

#include <stdbool.h>

/**
 * @brief 校验收支类型是否有效
 */
bool mf_daily_expense_rule_validate_type(const char* type);

/**
 * @brief 校验金额是否有效（正数）
 */
bool mf_daily_expense_rule_validate_amount(double amount);

/**
 * @brief 校验必填字段是否齐全
 */
bool mf_daily_expense_rule_validate_required(
    long long category_id, long long asset_id, const char* type, double amount, const char* date);
