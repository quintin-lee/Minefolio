#pragma once

/**
 * @file rules.h
 * @brief 账户领域业务规则 (Domain Account Rules)
 */

#include "domain/account/entity.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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

/**
 * @brief 解析账户级权限级别字符串
 */
mf_account_perm_level_t mf_account_rule_parse_perm_level(const char* level_str);

/**
 * @brief 将账户级权限级别转换为字符串常量
 */
const char* mf_account_rule_perm_level_to_string(mf_account_perm_level_t level);

/**
 * @brief 计算最终生效的账户权限级别
 * @param configured_level 账户级显式配置
 * @param ledger_role 成员在账本中的角色 ("owner", "editor", "viewer")
 */
mf_account_perm_level_t mf_account_rule_effective_perm(mf_account_perm_level_t configured_level,
                                                       const char*             ledger_role);

/**
 * @brief 判定是否具备只读可见权限
 */
bool mf_account_rule_can_read(mf_account_perm_level_t effective_perm);

/**
 * @brief 判定是否具备流水记账权限
 */
bool mf_account_rule_can_write_tx(mf_account_perm_level_t effective_perm);

/**
 * @brief 判定是否具备账户管理权限
 */
bool mf_account_rule_can_manage(mf_account_perm_level_t effective_perm);

/**
 * @brief 对银行卡号/账号字符串执行字段级脱敏（保留末4位，前置字符替换为*）
 */
void mf_account_rule_mask_account_no(const char* in, char* out, size_t out_len);
