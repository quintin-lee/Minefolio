/**
 * @file rules.c
 * @brief 账本业务规则实现 (Domain Ledger Rules)
 */

#include "domain/ledger/rules.h"
#include <string.h>

bool
mf_ledger_rule_validate_name(const char* name)
{
    if (!name || !name[0]) {
        return false;
    }
    if (strlen(name) > 128) {
        return false;
    }
    return true;
}

bool
mf_ledger_rule_validate_role(const char* role)
{
    if (!role || !role[0]) {
        return false;
    }
    return strcmp(role, "editor") == 0 || strcmp(role, "viewer") == 0;
}

bool
mf_ledger_rule_can_manage(const char* role)
{
    return role && strcmp(role, "owner") == 0;
}

bool
mf_ledger_rule_can_remove_member(bool is_owner, bool is_self)
{
    /* Owner 可以移除其他人，但不能移除自己 */
    if (is_owner && !is_self) {
        return true;
    }
    /* 非 owner 只能移除自己（退出） */
    if (!is_owner && is_self) {
        return true;
    }
    return false;
}

bool
mf_ledger_rule_owner_can_leave(bool is_owner)
{
    /* Owner 不能离开，应删除账本 */
    return !is_owner;
}
