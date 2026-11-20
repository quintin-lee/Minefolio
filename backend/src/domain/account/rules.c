#include "domain/account/rules.h"
#include <stdio.h>
#include <string.h>

bool
mf_account_rule_validate_name(const char* name)
{
    return name && name[0] && strlen(name) <= 64;
}

bool
mf_account_rule_is_valid_institution(const char* institution)
{
    if (!institution || !institution[0]) {
        return false;
    }
    return strcmp(institution, "bank_card") == 0 || strcmp(institution, "platform") == 0 ||
           strcmp(institution, "broker") == 0 || strcmp(institution, "fund_platform") == 0 ||
           strcmp(institution, "cash_wallet") == 0 || strcmp(institution, "other") == 0;
}

bool
mf_account_rule_can_delete(bool has_assets, bool has_transactions)
{
    return !has_assets && !has_transactions;
}

mf_account_perm_level_t
mf_account_rule_parse_perm_level(const char* level_str)
{
    if (!level_str || !level_str[0]) {
        return MF_ACCOUNT_PERM_INHERIT;
    }
    if (strcmp(level_str, "none") == 0) {
        return MF_ACCOUNT_PERM_NONE;
    }
    if (strcmp(level_str, "viewer") == 0) {
        return MF_ACCOUNT_PERM_VIEWER;
    }
    if (strcmp(level_str, "operator") == 0) {
        return MF_ACCOUNT_PERM_OPERATOR;
    }
    if (strcmp(level_str, "manager") == 0) {
        return MF_ACCOUNT_PERM_MANAGER;
    }
    return MF_ACCOUNT_PERM_INHERIT;
}

const char*
mf_account_rule_perm_level_to_string(mf_account_perm_level_t level)
{
    switch (level) {
    case MF_ACCOUNT_PERM_NONE:
        return "none";
    case MF_ACCOUNT_PERM_VIEWER:
        return "viewer";
    case MF_ACCOUNT_PERM_OPERATOR:
        return "operator";
    case MF_ACCOUNT_PERM_MANAGER:
        return "manager";
    case MF_ACCOUNT_PERM_INHERIT:
    default:
        return "inherit";
    }
}

mf_account_perm_level_t
mf_account_rule_effective_perm(mf_account_perm_level_t configured_level, const char* ledger_role)
{
    /* 账本拥有者对下属所有账户具备最高的 manager 权限 */
    if (ledger_role && strcmp(ledger_role, "owner") == 0) {
        return MF_ACCOUNT_PERM_MANAGER;
    }

    /* 显式配置了账户级权限 */
    if (configured_level != MF_ACCOUNT_PERM_INHERIT) {
        return configured_level;
    }

    /* 默认继承账本级角色 */
    if (ledger_role && strcmp(ledger_role, "editor") == 0) {
        return MF_ACCOUNT_PERM_OPERATOR;
    }
    if (ledger_role && strcmp(ledger_role, "viewer") == 0) {
        return MF_ACCOUNT_PERM_VIEWER;
    }

    return MF_ACCOUNT_PERM_NONE;
}

bool
mf_account_rule_can_read(mf_account_perm_level_t effective_perm)
{
    return effective_perm >= MF_ACCOUNT_PERM_VIEWER;
}

bool
mf_account_rule_can_write_tx(mf_account_perm_level_t effective_perm)
{
    return effective_perm >= MF_ACCOUNT_PERM_OPERATOR;
}

bool
mf_account_rule_can_manage(mf_account_perm_level_t effective_perm)
{
    return effective_perm >= MF_ACCOUNT_PERM_MANAGER;
}

void
mf_account_rule_mask_account_no(const char* in, char* out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    if (!in || !in[0]) {
        out[0] = '\0';
        return;
    }

    size_t len = strlen(in);
    if (len <= 4) {
        snprintf(out, out_len, "****");
        return;
    }

    size_t mask_cnt = len - 4;
    size_t write_idx = 0;
    for (size_t i = 0; i < mask_cnt && write_idx + 1 < out_len; i++) {
        out[write_idx++] = '*';
    }
    for (size_t i = mask_cnt; i < len && write_idx + 1 < out_len; i++) {
        out[write_idx++] = in[i];
    }
    out[write_idx] = '\0';
}
