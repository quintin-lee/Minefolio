#pragma once

/**
 * @file rules.h
 * @brief 账户领域业务规则 (Domain Account Rules)
 */

#include <stdbool.h>
#include <stdint.h>

/** 合法的账户机构类型枚举 */
#define MF_ACCOUNT_VALID_INSTITUTIONS "bank_card,platform,broker,fund_platform,cash_wallet,other"

/**
 * @brief 校验账户名称合法性
 */
bool mf_account_rule_validate_name(const char* name);

/**
 * @brief 校验机构类型合法性
 */
bool mf_account_rule_is_valid_institution(const char* institution);

/**
 * @brief 判断是否可以删除该账户（无资产且无交易时可删）
 */
bool mf_account_rule_can_delete(bool has_assets, bool has_transactions);
