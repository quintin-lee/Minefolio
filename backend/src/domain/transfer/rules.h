/**
 * @file rules.h
 * @brief 转账业务规则声明 (Domain Transfer Rules)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 校验转账必填字段
 */
bool
mf_transfer_rule_validate_required(int64_t from_id, int64_t to_id, double amount, const char* date);

/**
 * @brief 校验转出和转入不能是同一个资产
 */
bool mf_transfer_rule_validate_different_assets(int64_t from_id, int64_t to_id);
