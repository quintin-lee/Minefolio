/**
 * @file rules.c
 * @brief 日常收支业务规则实现 (Domain Daily Expense Rules)
 */

#include "domain/daily_expense/rules.h"
#include <string.h>

bool
mf_daily_expense_rule_validate_type(const char* type)
{
    return type && (strcmp(type, "income") == 0 || strcmp(type, "expense") == 0);
}

bool
mf_daily_expense_rule_validate_amount(double amount)
{
    return amount > 0;
}

bool
mf_daily_expense_rule_validate_required(
    long long category_id, long long asset_id, const char* type, double amount, const char* date)
{
    return category_id > 0 && asset_id > 0 && type && type[0] && amount > 0 && date && date[0];
}
