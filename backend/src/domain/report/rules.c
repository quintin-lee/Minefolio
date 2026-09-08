/**
 * @file rules.c
 * @brief 报表业务规则实现 (Domain Report Rules)
 */

#include "domain/report/rules.h"

bool
mf_report_rule_validate_months(int months)
{
    return months > 0 && months <= 24;
}

bool
mf_report_rule_validate_year(const char* year_str)
{
    return year_str && year_str[0] && year_str[0] >= '2' && year_str[0] <= '9';
}
