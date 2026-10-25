#include "domain/account/rules.h"
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
