#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "domain/auth/entity.h"
#include "domain/auth/rules.h"

static void test_username_validation(void) {
    char err[256];

    assert(mf_auth_rule_validate_username(NULL, err, sizeof(err)) != 0);
    assert(mf_auth_rule_validate_username("", err, sizeof(err)) != 0);
    assert(mf_auth_rule_validate_username("a", err, sizeof(err)) != 0);
    assert(mf_auth_rule_validate_username("ab", err, sizeof(err)) == 0);
    assert(mf_auth_rule_validate_username("admin", err, sizeof(err)) == 0);

    char long_name[100];
    memset(long_name, 'x', 70);
    long_name[70] = '\0';
    assert(mf_auth_rule_validate_username(long_name, err, sizeof(err)) != 0);

    printf("PASS: test_username_validation\n");
}

static void test_password_validation(void) {
    char err[256];

    assert(mf_auth_rule_validate_password(NULL, err, sizeof(err)) != 0);
    assert(mf_auth_rule_validate_password("", err, sizeof(err)) != 0);
    assert(mf_auth_rule_validate_password("12345", err, sizeof(err)) != 0);
    assert(mf_auth_rule_validate_password("123456", err, sizeof(err)) == 0);
    assert(mf_auth_rule_validate_password("StrongPassword123!", err, sizeof(err)) == 0);

    printf("PASS: test_password_validation\n");
}

static void test_registration_policy(void) {
    /* 1. 初始化阶段 (0 用户): 必须允许注册 */
    assert(mf_auth_rule_can_register(0, false) == true);
    assert(mf_auth_rule_can_register(0, true) == true);

    /* 2. 存量用户阶段 (>0 用户): 由配置决定 */
    assert(mf_auth_rule_can_register(1, false) == false);
    assert(mf_auth_rule_can_register(1, true) == true);
    assert(mf_auth_rule_can_register(5, false) == false);

    printf("PASS: test_registration_policy\n");
}

static void test_backup_code_verification(void) {
    char backup_codes[16][16] = {
        "ABC12345",
        "DEF67890",
        "GHI11223"
    };
    int matched_idx = -1;

    /* 匹配第 2 个备用码 */
    bool ok = mf_auth_rule_verify_backup_code(backup_codes, 3, "DEF67890", &matched_idx);
    assert(ok == true);
    assert(matched_idx == 1);

    /* 不匹配错误码 */
    ok = mf_auth_rule_verify_backup_code(backup_codes, 3, "WRONG999", &matched_idx);
    assert(ok == false);

    printf("PASS: test_backup_code_verification\n");
}

int main(void) {
    test_username_validation();
    test_password_validation();
    test_registration_policy();
    test_backup_code_verification();
    printf("All domain auth tests passed successfully!\n");
    return 0;
}
