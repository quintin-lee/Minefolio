#include "application/account/usecases.h"
#include "domain/account/rules.h"
#include "domain/auth/repository.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "infrastructure/repositories/account_repo_impl.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* TEST_DB_PATH = "/tmp/test_mf_account_rbac.db";

static csilk_db_pool_t* setup_test_db(void) {
    unlink(TEST_DB_PATH);
    char shm[256], wal[256];
    snprintf(shm, sizeof(shm), "%s-shm", TEST_DB_PATH);
    snprintf(wal, sizeof(wal), "%s-wal", TEST_DB_PATH);
    unlink(shm);
    unlink(wal);

    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", TEST_DB_PATH);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_account_rbac_32bytes!");

    csilk_db_pool_t* pool = NULL;
    int rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);

    rc = db_run_migrations(pool);
    assert(rc == 0);

    return pool;
}

static void test_domain_rules(void) {
    printf("--- Testing Domain Account RBAC Rules ---\n");

    // 1. parse_perm_level
    assert(mf_account_rule_parse_perm_level("none") == MF_ACCOUNT_PERM_NONE);
    assert(mf_account_rule_parse_perm_level("viewer") == MF_ACCOUNT_PERM_VIEWER);
    assert(mf_account_rule_parse_perm_level("operator") == MF_ACCOUNT_PERM_OPERATOR);
    assert(mf_account_rule_parse_perm_level("manager") == MF_ACCOUNT_PERM_MANAGER);
    assert(mf_account_rule_parse_perm_level(NULL) == MF_ACCOUNT_PERM_INHERIT);
    assert(mf_account_rule_parse_perm_level("unknown") == MF_ACCOUNT_PERM_INHERIT);
    printf("  [OK] parse_perm_level\n");

    // 2. perm_level_to_string
    assert(strcmp(mf_account_rule_perm_level_to_string(MF_ACCOUNT_PERM_NONE), "none") == 0);
    assert(strcmp(mf_account_rule_perm_level_to_string(MF_ACCOUNT_PERM_VIEWER), "viewer") == 0);
    assert(strcmp(mf_account_rule_perm_level_to_string(MF_ACCOUNT_PERM_OPERATOR), "operator") == 0);
    assert(strcmp(mf_account_rule_perm_level_to_string(MF_ACCOUNT_PERM_MANAGER), "manager") == 0);
    assert(strcmp(mf_account_rule_perm_level_to_string(MF_ACCOUNT_PERM_INHERIT), "inherit") == 0);
    printf("  [OK] perm_level_to_string\n");

    // 3. effective_perm
    // Explicit overrides
    assert(mf_account_rule_effective_perm(MF_ACCOUNT_PERM_VIEWER, "editor") == MF_ACCOUNT_PERM_VIEWER);
    assert(mf_account_rule_effective_perm(MF_ACCOUNT_PERM_OPERATOR, "viewer") == MF_ACCOUNT_PERM_OPERATOR);
    assert(mf_account_rule_effective_perm(MF_ACCOUNT_PERM_NONE, "editor") == MF_ACCOUNT_PERM_NONE);
    // Inheritance
    assert(mf_account_rule_effective_perm(MF_ACCOUNT_PERM_INHERIT, "owner") == MF_ACCOUNT_PERM_MANAGER);
    assert(mf_account_rule_effective_perm(MF_ACCOUNT_PERM_INHERIT, "editor") == MF_ACCOUNT_PERM_OPERATOR);
    assert(mf_account_rule_effective_perm(MF_ACCOUNT_PERM_INHERIT, "viewer") == MF_ACCOUNT_PERM_VIEWER);
    assert(mf_account_rule_effective_perm(MF_ACCOUNT_PERM_INHERIT, "invalid") == MF_ACCOUNT_PERM_NONE);
    assert(mf_account_rule_effective_perm(MF_ACCOUNT_PERM_INHERIT, NULL) == MF_ACCOUNT_PERM_NONE);
    printf("  [OK] effective_perm\n");

    // 4. Permission capabilities
    assert(!mf_account_rule_can_read(MF_ACCOUNT_PERM_NONE));
    assert(mf_account_rule_can_read(MF_ACCOUNT_PERM_VIEWER));
    assert(mf_account_rule_can_read(MF_ACCOUNT_PERM_OPERATOR));
    assert(mf_account_rule_can_read(MF_ACCOUNT_PERM_MANAGER));

    assert(!mf_account_rule_can_write_tx(MF_ACCOUNT_PERM_NONE));
    assert(!mf_account_rule_can_write_tx(MF_ACCOUNT_PERM_VIEWER));
    assert(mf_account_rule_can_write_tx(MF_ACCOUNT_PERM_OPERATOR));
    assert(mf_account_rule_can_write_tx(MF_ACCOUNT_PERM_MANAGER));

    assert(!mf_account_rule_can_manage(MF_ACCOUNT_PERM_NONE));
    assert(!mf_account_rule_can_manage(MF_ACCOUNT_PERM_VIEWER));
    assert(!mf_account_rule_can_manage(MF_ACCOUNT_PERM_OPERATOR));
    assert(mf_account_rule_can_manage(MF_ACCOUNT_PERM_MANAGER));
    printf("  [OK] permission capabilities\n");

    // 5. mask_account_no
    char buf[64];
    mf_account_rule_mask_account_no("62258888", buf, sizeof(buf));
    assert(strcmp(buf, "****8888") == 0);

    mf_account_rule_mask_account_no("123", buf, sizeof(buf));
    assert(strcmp(buf, "****") == 0);

    mf_account_rule_mask_account_no("", buf, sizeof(buf));
    assert(strcmp(buf, "") == 0);

    mf_account_rule_mask_account_no(NULL, buf, sizeof(buf));
    assert(strcmp(buf, "") == 0);
    printf("  [OK] mask_account_no\n");
}

static void test_repo_crud(csilk_db_pool_t* pool) {
    printf("--- Testing Account Permission Repository CRUD ---\n");

    int64_t user_id = 0;
    int rc = mf_auth_repo_create(pool, "test_perm_user", "hash_pw_test", &user_id);
    assert(rc == 0 && user_id > 0);

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    // Create a real ledger and account for foreign key integrity
    int exec_rc = csilk_db_exec_param(pool, "INSERT INTO ledgers (owner_id, name, is_default) VALUES (?, 'RepoTestLedger', 1);",
                        (const char*[]){uid_str, NULL});
    assert(exec_rc == 0);
    csilk_json_t* lrow = csilk_db_query_param_json(pool, "SELECT id FROM ledgers WHERE owner_id=? AND name='RepoTestLedger';",
                                                   (const char*[]){uid_str, NULL});
    assert(lrow && csilk_json_array_size(lrow) > 0);
    int64_t ledger_id = db_get_int(csilk_json_array_get(lrow, 0), "id");
    csilk_json_free(lrow);
    assert(ledger_id > 0);

    char lid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    csilk_db_exec_param(pool,
        "INSERT INTO accounts (user_id, ledger_id, name, institution, currency) VALUES (?, ?, 'RepoTestAccount', 'bank_card', 'CNY');",
        (const char*[]){uid_str, lid_str, NULL});
    csilk_json_t* arow = csilk_db_query_param_json(pool, "SELECT id FROM accounts WHERE user_id=? AND name='RepoTestAccount';",
                                                   (const char*[]){uid_str, NULL});
    assert(arow && csilk_json_array_size(arow) > 0);
    int64_t account_id = db_get_int(csilk_json_array_get(arow, 0), "id");
    csilk_json_free(arow);
    assert(account_id > 0);

    // 1. Initial state: no record
    csilk_json_t* perm = account_perm_get(pool, ledger_id, account_id, user_id);
    assert(perm == NULL || csilk_json_array_size(perm) == 0);
    if (perm) csilk_json_free(perm);

    // 2. Upsert permission
    rc = account_perm_upsert(pool, ledger_id, account_id, user_id, "viewer", 1, 1);
    assert(rc == 0);

    perm = account_perm_get(pool, ledger_id, account_id, user_id);
    assert(perm != NULL && csilk_json_array_size(perm) == 1);
    csilk_json_t* row = csilk_json_array_get(perm, 0);
    const char* lvl = csilk_json_get_string(row, "permission_level");
    assert(lvl != NULL && strcmp(lvl, "viewer") == 0);
    assert(csilk_json_get_int(row, "mask_balance") == 1);
    assert(csilk_json_get_int(row, "mask_account_no") == 1);
    csilk_json_free(perm);
    printf("  [OK] account_perm_upsert insert\n");

    // 3. Update existing
    rc = account_perm_upsert(pool, ledger_id, account_id, user_id, "operator", 0, 1);
    assert(rc == 0);

    perm = account_perm_get(pool, ledger_id, account_id, user_id);
    assert(perm != NULL && csilk_json_array_size(perm) == 1);
    row = csilk_json_array_get(perm, 0);
    lvl = csilk_json_get_string(row, "permission_level");
    assert(lvl != NULL && strcmp(lvl, "operator") == 0);
    assert(csilk_json_get_int(row, "mask_balance") == 0);
    assert(csilk_json_get_int(row, "mask_account_no") == 1);
    csilk_json_free(perm);
    printf("  [OK] account_perm_upsert update\n");

    // 4. List by account
    csilk_json_t* list = account_perm_list_by_account(pool, ledger_id, account_id);
    assert(list != NULL);
    assert(csilk_json_array_size(list) >= 1);
    csilk_json_free(list);
    printf("  [OK] account_perm_list_by_account\n");

    // 5. List by user
    list = account_perm_list_by_user(pool, ledger_id, user_id);
    assert(list != NULL);
    assert(csilk_json_array_size(list) >= 1);
    csilk_json_free(list);
    printf("  [OK] account_perm_list_by_user\n");

    // 6. Delete permission
    rc = account_perm_delete(pool, ledger_id, account_id, user_id);
    assert(rc == 0);

    perm = account_perm_get(pool, ledger_id, account_id, user_id);
    assert(perm == NULL || csilk_json_array_size(perm) == 0);
    if (perm) csilk_json_free(perm);
    printf("  [OK] account_perm_delete\n");
}

static void test_usecases_and_masking(csilk_db_pool_t* pool) {
    printf("--- Testing Account Use Cases & Field Masking ---\n");

    int64_t owner_id = 0;
    int rc = mf_auth_repo_create(pool, "alice_owner", "hash_pw_alice", &owner_id);
    assert(rc == 0 && owner_id > 0);

    int64_t member_id = 0;
    rc = mf_auth_repo_create(pool, "bob_member", "hash_pw_bob", &member_id);
    assert(rc == 0 && member_id > 0);

    // Create shared ledger owned by Alice
    char s_oid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    csilk_db_exec_param(pool, "INSERT INTO ledgers (owner_id, name, is_default) VALUES (?, '家庭账本', 0);",
                        (const char*[]){s_oid, NULL});
    // Find ledger id
    csilk_json_t* ledger_row = csilk_db_query_param_json(pool, "SELECT id FROM ledgers WHERE owner_id=? AND name='家庭账本';",
                                                        (const char*[]){s_oid, NULL});
    assert(ledger_row != NULL && csilk_json_array_size(ledger_row) > 0);
    int64_t ledger_id = db_get_int(csilk_json_array_get(ledger_row, 0), "id");
    csilk_json_free(ledger_row);
    assert(ledger_id > 0);

    // Add Alice (owner) and Bob (editor) to ledger_members
    char s_lid[32], s_mid[32];
    snprintf(s_lid, sizeof(s_lid), "%lld", (long long)ledger_id);
    snprintf(s_mid, sizeof(s_mid), "%lld", (long long)member_id);

    csilk_db_exec_param(pool,
        "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner');",
        (const char*[]){s_lid, s_oid, NULL});

    csilk_db_exec_param(pool,
        "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'editor');",
        (const char*[]){s_lid, s_mid, NULL});

    // Alice creates Account A ("招商银行")
    account_usecase_result_t res;
    create_account_cmd_t cmd_a = {
        .user_id = owner_id,
        .ledger_id = ledger_id,
        .name = "招商银行储蓄卡",
        .institution = "bank_card",
        .account_no = "6225888812345678",
        .currency = "CNY",
    };
    rc = account_usecase_create(pool, &cmd_a, &res);
    assert(rc == 0 && res.id > 0);
    int64_t acct_a = res.id;

    // Alice creates Account B ("私房钱账户")
    create_account_cmd_t cmd_b = {
        .user_id = owner_id,
        .ledger_id = ledger_id,
        .name = "私房钱账户",
        .institution = "cash_wallet",
        .account_no = "CASH001",
        .currency = "CNY",
    };
    rc = account_usecase_create(pool, &cmd_b, &res);
    assert(rc == 0 && res.id > 0);
    int64_t acct_b = res.id;

    // Case 1: Before explicit config, Bob inherits ledger 'editor' role (operator permissions) on both accounts
    rc = account_usecase_check_permission(pool, ledger_id, member_id, acct_a, MF_ACCOUNT_ACTION_WRITE_TX);
    assert(rc == 0);
    rc = account_usecase_check_permission(pool, ledger_id, member_id, acct_b, MF_ACCOUNT_ACTION_WRITE_TX);
    assert(rc == 0);
    printf("  [OK] Default inheritance before config\n");

    // Case 2: Alice configures Account A: Bob gets 'viewer', mask_balance=1, mask_account_no=1
    rc = account_permission_usecase_set(pool, owner_id, ledger_id, acct_a, member_id, "viewer", 1, 1, &res);
    assert(rc == 0 && res.code == 0);

    // Case 3: Alice configures Account B: Bob gets 'none'
    rc = account_permission_usecase_set(pool, owner_id, ledger_id, acct_b, member_id, "none", 0, 0, &res);
    assert(rc == 0 && res.code == 0);

    // Verify Bob's permission checks on Account A:
    // Can read (viewer)
    assert(account_usecase_check_permission(pool, ledger_id, member_id, acct_a, MF_ACCOUNT_ACTION_READ) == 0);
    // Cannot write transactions (requires operator)
    assert(account_usecase_check_permission(pool, ledger_id, member_id, acct_a, MF_ACCOUNT_ACTION_WRITE_TX) == 1004);
    // Cannot manage (requires manager)
    assert(account_usecase_check_permission(pool, ledger_id, member_id, acct_a, MF_ACCOUNT_ACTION_MANAGE) == 1004);

    // Verify Bob's permission checks on Account B:
    // Cannot even read (none)
    assert(account_usecase_check_permission(pool, ledger_id, member_id, acct_b, MF_ACCOUNT_ACTION_READ) == 1004);
    printf("  [OK] Permission enforcement check\n");

    // Case 4: Bob queries account list
    csilk_json_t* bob_accounts = NULL;
    int64_t total = 0;
    rc = account_usecase_query(pool, member_id, ledger_id, 0, &bob_accounts, &total);
    assert(rc == 0 && bob_accounts != NULL);
    int size = csilk_json_array_size(bob_accounts);
    // Account B (none) MUST NOT appear in Bob's query results!
    bool found_a = false;
    bool found_b = false;
    for (int i = 0; i < size; i++) {
        csilk_json_t* item = csilk_json_array_get(bob_accounts, i);
        int64_t id = csilk_json_get_int(item, "id");
        if (id == acct_a) {
            found_a = true;
            // Verify masking on Account A
            assert(csilk_json_get_bool(item, "is_balance_masked") == true);
            assert(csilk_json_get_bool(item, "is_account_no_masked") == true);
            assert(csilk_json_get_number(item, "total_value") == 0.0);
            const char* masked_no = csilk_json_get_string(item, "account_no");
            assert(masked_no != NULL && strcmp(masked_no, "************5678") == 0);
            const char* plvl = csilk_json_get_string(item, "permission_level");
            assert(plvl != NULL && strcmp(plvl, "viewer") == 0);
        }
        if (id == acct_b) {
            found_b = true;
        }
    }
    assert(found_a == true);
    assert(found_b == false);
    csilk_json_free(bob_accounts);
    printf("  [OK] Account list masking and none-account hiding\n");

    // Case 5: Alice (Owner) queries account list -> no masking!
    csilk_json_t* alice_accounts = NULL;
    rc = account_usecase_query(pool, owner_id, ledger_id, 0, &alice_accounts, &total);
    assert(rc == 0 && alice_accounts != NULL);
    int alice_size = csilk_json_array_size(alice_accounts);
    found_a = false;
    found_b = false;
    for (int i = 0; i < alice_size; i++) {
        csilk_json_t* item = csilk_json_array_get(alice_accounts, i);
        int64_t id = csilk_json_get_int(item, "id");
        if (id == acct_a) {
            found_a = true;
            assert(csilk_json_get_bool(item, "is_balance_masked") == false);
            assert(csilk_json_get_bool(item, "is_account_no_masked") == false);
            const char* no = csilk_json_get_string(item, "account_no");
            assert(no != NULL && strcmp(no, "6225888812345678") == 0);
            const char* plvl = csilk_json_get_string(item, "permission_level");
            assert(plvl != NULL && strcmp(plvl, "manager") == 0);
        }
        if (id == acct_b) {
            found_b = true;
        }
    }
    assert(found_a && found_b);
    csilk_json_free(alice_accounts);
    printf("  [OK] Owner sees unmasked data and all accounts\n");
}

int main(void) {
    printf("========================================\n");
    printf("Starting Account RBAC & Field Masking Tests\n");
    printf("========================================\n");

    test_domain_rules();

    csilk_db_pool_t* pool = setup_test_db();
    test_repo_crud(pool);
    test_usecases_and_masking(pool);

    printf("========================================\n");
    printf("All Account RBAC Tests Passed Successfully!\n");
    printf("========================================\n");
    return 0;
}
