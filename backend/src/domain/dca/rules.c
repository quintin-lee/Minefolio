/**
 * @file rules.c
 * @brief 定投计划业务规则实现 (Domain DCA Rules)
 */

#include "domain/dca/rules.h"
#include <string.h>

bool
mf_dca_rule_validate_plan_required(int64_t     target_asset_id,
                                   int64_t     funding_asset_id,
                                   const char* name,
                                   double      amount)
{
    return target_asset_id > 0 && funding_asset_id > 0 && name && name[0] && amount > 0.0;
}

bool
mf_dca_rule_validate_status(const char* status)
{
    if (!status || !status[0]) {
        return false;
    }
    return strcmp(status, "active") == 0 || strcmp(status, "paused") == 0 ||
           strcmp(status, "completed") == 0;
}

double
mf_dca_rule_calc_profit_rate(double current_value, double total_invested)
{
    if (total_invested <= 0.0) {
        return 0.0;
    }
    return (current_value - total_invested) / total_invested;
}

bool
mf_dca_rule_check_profit_target(double profit_rate, double target_profit_rate)
{
    return target_profit_rate > 0.0 && profit_rate >= target_profit_rate;
}
