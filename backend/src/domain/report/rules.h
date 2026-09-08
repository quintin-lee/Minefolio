/**
 * @file rules.h
 * @brief 报表业务规则声明 (Domain Report Rules)
 */

#pragma once

#include <stdbool.h>

/**
 * @brief 校验趋势月份参数是否有效
 */
bool mf_report_rule_validate_months(int months);

/**
 * @brief 校验年份参数是否有效
 */
bool mf_report_rule_validate_year(const char* year_str);
