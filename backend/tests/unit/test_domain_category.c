#include "domain/category/rules.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== Starting Domain Category Rules Tests ===\n");

    // 1. validate_name
    assert(!mf_category_rule_validate_name(NULL));
    assert(!mf_category_rule_validate_name(""));
    assert(mf_category_rule_validate_name("餐饮美食"));
    assert(mf_category_rule_validate_name("Salary"));

    char long_name[256];
    memset(long_name, 'c', sizeof(long_name));
    long_name[128] = '\0';
    assert(mf_category_rule_validate_name(long_name));
    long_name[128] = 'c';
    long_name[129] = '\0';
    assert(!mf_category_rule_validate_name(long_name));
    printf("  [OK] mf_category_rule_validate_name\n");

    // 2. validate_type
    assert(!mf_category_rule_validate_type(NULL));
    assert(!mf_category_rule_validate_type(""));
    assert(!mf_category_rule_validate_type("unknown"));
    assert(mf_category_rule_validate_type("expense"));
    assert(mf_category_rule_validate_type("income"));
    assert(mf_category_rule_validate_type("asset"));
    assert(mf_category_rule_validate_type("transaction"));
    printf("  [OK] mf_category_rule_validate_type\n");

    // 3. can_delete
    assert(mf_category_rule_can_delete(0));
    assert(!mf_category_rule_can_delete(1));
    assert(!mf_category_rule_can_delete(5));
    printf("  [OK] mf_category_rule_can_delete\n");

    printf("=== Domain Category Rules Tests Passed ===\n");
    return 0;
}
