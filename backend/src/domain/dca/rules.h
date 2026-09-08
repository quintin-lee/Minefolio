/**
 * @file rules.h
 * @brief 定投计划业务规则声明 (Domain DCA Rules)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 校验定投计划必填字段
 */
bool mf_dca_rule_validate_plan_required(int64_t     target_asset_id,
                                        int64_t     funding_asset_id,
                                        const char* name,
                                        double      amount);

/**
 * @brief 校验状态是否有效
 */
bool mf_dca_rule_validate_status(const char* status);

/**
 * @brief 计算收益率
 */
double mf_dca_rule_calc_profit_rate(double current_value, double total_invested);

/**
 * @brief 检查是否达到止盈目标
 */
bool mf_dca_rule_check_profit_target(double profit_rate, double target_profit_rate);
