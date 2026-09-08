/**
 * @file rules.h
 * @brief 账本业务规则声明 (Domain Ledger Rules)
 */

#pragma once

#include <stdbool.h>

/**
 * @brief 校验账本名称是否有效
 */
bool mf_ledger_rule_validate_name(const char* name);

/**
 * @brief 校验角色是否有效
 */
bool mf_ledger_rule_validate_role(const char* role);

/**
 * @brief 检查是否有权执行 owner-only 操作
 */
bool mf_ledger_rule_can_manage(const char* role);

/**
 * @brief 检查是否可以移除成员
 * @param is_owner 当前用户是否为 owner
 * @param is_self 是否移除自己
 */
bool mf_ledger_rule_can_remove_member(bool is_owner, bool is_self);

/**
 * @brief 检查 owner 是否可以离开（不允许，应删除账本）
 */
bool mf_ledger_rule_owner_can_leave(bool is_owner);
