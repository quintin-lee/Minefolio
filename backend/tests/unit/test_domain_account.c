#include "domain/account/rules.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== Starting Domain Account Rules Tests ===\n");

    // 1. validate_name
    assert(!mf_account_rule_validate_name(NULL));
    assert(!mf_account_rule_validate_name(""));
    assert(mf_account_rule_validate_name("招商银行储蓄卡"));
    assert(mf_account_rule_validate_name("Alipay Main"));

    char long_name[128];
    memset(long_name, 'a', sizeof(long_name));
    long_name[64] = '\0';
    assert(mf_account_rule_validate_name(long_name));
    long_name[64] = 'a';
    long_name[65] = '\0';
    assert(!mf_account_rule_validate_name(long_name));
    printf("  [OK] mf_account_rule_validate_name\n");

    // 2. is_valid_institution
    assert(!mf_account_rule_is_valid_institution(NULL));
    assert(!mf_account_rule_is_valid_institution(""));
    assert(!mf_account_rule_is_valid_institution("invalid_inst"));
    assert(mf_account_rule_is_valid_institution("bank_card"));
    assert(mf_account_rule_is_valid_institution("platform"));
    assert(mf_account_rule_is_valid_institution("broker"));
    assert(mf_account_rule_is_valid_institution("fund_platform"));
    assert(mf_account_rule_is_valid_institution("cash_wallet"));
    assert(mf_account_rule_is_valid_institution("other"));
    printf("  [OK] mf_account_rule_is_valid_institution\n");

    // 3. can_delete
    assert(mf_account_rule_can_delete(false, false));
    assert(!mf_account_rule_can_delete(true, false));
    assert(!mf_account_rule_can_delete(false, true));
    assert(!mf_account_rule_can_delete(true, true));
    printf("  [OK] mf_account_rule_can_delete\n");

    printf("=== Domain Account Rules Tests Passed ===\n");
    return 0;
}
