/**
 * @file rules.c
 * @brief 转账业务规则实现 (Domain Transfer Rules)
 */

#include "domain/transfer/rules.h"

bool
mf_transfer_rule_validate_required(int64_t from_id, int64_t to_id, double amount, const char* date)
{
    return from_id > 0 && to_id > 0 && amount > 0 && date && date[0];
}

bool
mf_transfer_rule_validate_different_assets(int64_t from_id, int64_t to_id)
{
    return from_id != to_id;
}
