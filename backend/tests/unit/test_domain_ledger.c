#include "domain/ledger/rules.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== Starting Domain Ledger Rules Tests ===\n");

    // 1. validate_name
    assert(!mf_ledger_rule_validate_name(NULL));
    assert(!mf_ledger_rule_validate_name(""));
    assert(mf_ledger_rule_validate_name("默认账本"));
    assert(mf_ledger_rule_validate_name("家庭账本 2026"));

    char long_name[256];
    memset(long_name, 'l', sizeof(long_name));
    long_name[128] = '\0';
    assert(mf_ledger_rule_validate_name(long_name));
    long_name[128] = 'l';
    long_name[129] = '\0';
    assert(!mf_ledger_rule_validate_name(long_name));
    printf("  [OK] mf_ledger_rule_validate_name\n");

    // 2. validate_role
    assert(!mf_ledger_rule_validate_role(NULL));
    assert(!mf_ledger_rule_validate_role(""));
    assert(!mf_ledger_rule_validate_role("owner")); // only editor or viewer are assignable member roles
    assert(mf_ledger_rule_validate_role("editor"));
    assert(mf_ledger_rule_validate_role("viewer"));
    printf("  [OK] mf_ledger_rule_validate_role\n");

    // 3. can_manage
    assert(!mf_ledger_rule_can_manage(NULL));
    assert(!mf_ledger_rule_can_manage(""));
    assert(!mf_ledger_rule_can_manage("editor"));
    assert(!mf_ledger_rule_can_manage("viewer"));
    assert(mf_ledger_rule_can_manage("owner"));
    printf("  [OK] mf_ledger_rule_can_manage\n");

    // 4. can_remove_member
    // Owner removing someone else -> true
    assert(mf_ledger_rule_can_remove_member(true, false));
    // Owner removing self -> false
    assert(!mf_ledger_rule_can_remove_member(true, true));
    // Non-owner removing self (leaving) -> true
    assert(mf_ledger_rule_can_remove_member(false, true));
    // Non-owner removing someone else -> false
    assert(!mf_ledger_rule_can_remove_member(false, false));
    printf("  [OK] mf_ledger_rule_can_remove_member\n");

    // 5. owner_can_leave
    assert(!mf_ledger_rule_owner_can_leave(true));
    assert(mf_ledger_rule_owner_can_leave(false));
    printf("  [OK] mf_ledger_rule_owner_can_leave\n");

    printf("=== Domain Ledger Rules Tests Passed ===\n");
    return 0;
}
