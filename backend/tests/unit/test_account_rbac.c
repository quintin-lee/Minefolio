#include "application/account/usecases.h"
#include "application/asset/usecases.h"
#include "application/transaction/usecases.h"
#include "application/daily_expense/usecases.h"
#include "application/dca/usecases.h"
#include "application/cashflow/usecases.h"
#include "domain/account/rules.h"
#include "domain/auth/repository.h"
#include "domain/ai/repository.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "infrastructure/repositories/account_repo_impl.h"
#include "infrastructure/repositories/dca_repo_impl.h"
#include "infrastructure/repositories/portfolio_repo_impl.h"
#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"
#include "services/report_asset_service.h"
#include "services/export_service.h"
#include "services/import_service.h"
#include "services/ai/tools/registry.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/context.h"
#include "services/ai/tools/batch_tool.h"
#include "services/ai/policy/confirmation.h"
#include "services/ai/policy/permission.h"
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

    // Case 6: Downstream RBAC enforcement across assets, transactions, expenses, reports, portfolio, and AI repos
    printf("--- Testing Downstream Account & Field RBAC Enforcement ---\n");
    // Seed categories in family ledger for Bob
    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES (?, ?, '活期存款', 'asset', 'cash');",
        (const char*[]){s_mid, s_lid, NULL});
    csilk_json_t* cat_row = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id=? AND ledger_id=? AND name='活期存款';",
        (const char*[]){s_mid, s_lid, NULL});
    assert(cat_row && csilk_json_array_size(cat_row) > 0);
    int64_t cash_cat_id = db_get_int(csilk_json_array_get(cat_row, 0), "id");
    csilk_json_free(cat_row);

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type) VALUES (?, ?, '餐饮美食', 'expense');",
        (const char*[]){s_mid, s_lid, NULL});
    cat_row = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id=? AND ledger_id=? AND name='餐饮美食';",
        (const char*[]){s_mid, s_lid, NULL});
    assert(cat_row && csilk_json_array_size(cat_row) > 0);
    int64_t exp_cat_id = db_get_int(csilk_json_array_get(cat_row, 0), "id");
    csilk_json_free(cat_row);

    // Temporarily grant Bob operator on both accounts to seed downstream entities
    assert(account_permission_usecase_set(pool, owner_id, ledger_id, acct_a, member_id, "operator", 0, 0, &res) == 0);
    assert(account_permission_usecase_set(pool, owner_id, ledger_id, acct_b, member_id, "operator", 0, 0, &res) == 0);

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t acmd_a = {
        .user_id = member_id,
        .ledger_id = ledger_id,
        .category_id = cash_cat_id,
        .account_id = acct_a,
        .name = "招行工资卡资产",
        .account_no = "6225000011112222",
        .current_value = 5000.0,
        .currency = "CNY",
    };
    assert(asset_usecase_create(pool, &acmd_a, &ares) == 0 && ares.id > 0);
    int64_t asset_a = ares.id;

    create_asset_cmd_t acmd_b = {
        .user_id = member_id,
        .ledger_id = ledger_id,
        .category_id = cash_cat_id,
        .account_id = acct_b,
        .name = "私房钱金库资产",
        .account_no = "9999888877776666",
        .current_value = 9000.0,
        .currency = "CNY",
    };
    assert(asset_usecase_create(pool, &acmd_b, &ares) == 0 && ares.id > 0);
    int64_t asset_b = ares.id;

    tx_usecase_result_t txres = {0};
    create_tx_cmd_t txcmd_a = {
        .user_id = member_id,
        .ledger_id = ledger_id,
        .asset_id = asset_a,
        .category_id = exp_cat_id,
        .type = "income",
        .amount = 200.0,
        .currency = "CNY",
        .date = "2026-11-20",
        .note = "奖金入账",
    };
    assert(tx_usecase_create(pool, &txcmd_a, &txres) == 0 && txres.created_id > 0);
    int64_t tx_a = txres.created_id;

    create_tx_cmd_t txcmd_b = {
        .user_id = member_id,
        .ledger_id = ledger_id,
        .asset_id = asset_b,
        .category_id = exp_cat_id,
        .type = "income",
        .amount = 800.0,
        .currency = "CNY",
        .date = "2026-11-20",
        .note = "私房钱入账",
    };
    assert(tx_usecase_create(pool, &txcmd_b, &txres) == 0 && txres.created_id > 0);

    daily_expense_usecase_result_t deres = {0};
    create_daily_expense_cmd_t decmd_a = {
        .user_id = member_id,
        .ledger_id = ledger_id,
        .category_id = exp_cat_id,
        .asset_id = asset_a,
        .expense_type = "expense",
        .amount = 100.0,
        .currency = "CNY",
        .expense_date = "2026-11-20",
        .note = "午餐",
    };
    assert(daily_expense_usecase_create(pool, &decmd_a, NULL, &deres) == 0);

    create_daily_expense_cmd_t decmd_b = {
        .user_id = member_id,
        .ledger_id = ledger_id,
        .category_id = exp_cat_id,
        .asset_id = asset_b,
        .expense_type = "expense",
        .amount = 400.0,
        .currency = "CNY",
        .expense_date = "2026-11-20",
        .note = "私密消费",
    };
    assert(daily_expense_usecase_create(pool, &decmd_b, NULL, &deres) == 0);

    csilk_json_t* exp_row = csilk_db_query_param_json(
        pool,
        "SELECT id FROM daily_expenses WHERE user_id=? AND note='午餐';",
        (const char*[]){s_mid, NULL});
    assert(exp_row && csilk_json_array_size(exp_row) == 1);
    int64_t exp_a = db_get_int(csilk_json_array_get(exp_row, 0), "id");
    csilk_json_free(exp_row);

    // Now Alice sets Account A -> viewer + mask_balance=1 + mask_account_no=1, Account B -> none
    assert(account_permission_usecase_set(pool, owner_id, ledger_id, acct_a, member_id, "viewer", 1, 1, &res) == 0);
    assert(account_permission_usecase_set(pool, owner_id, ledger_id, acct_b, member_id, "none", 0, 0, &res) == 0);

    // 6.1 Asset list: asset_b hidden, asset_a masked + account_id preserved
    csilk_json_t* asset_list_json = NULL;
    int64_t asset_total = 0;
    query_asset_filter_t afil = {.user_id = member_id, .page = 1, .page_size = 20};
    assert(asset_usecase_query(pool, &afil, &asset_list_json, &asset_total) == 0);
    assert(asset_total == 1 && csilk_json_array_size(asset_list_json) == 1);
    csilk_json_t* aitem = csilk_json_array_get(asset_list_json, 0);
    assert(db_get_int(aitem, "id") == asset_a);
    assert(db_get_int(aitem, "account_id") == acct_a);
    assert(csilk_json_get_bool(aitem, "is_balance_masked") == true);
    assert(csilk_json_get_bool(aitem, "is_account_no_masked") == true);
    assert(db_get_num(aitem, "current_value") == 0.0);
    assert(strcmp(csilk_json_get_string(aitem, "account_no"), "************2222") == 0);
    csilk_json_free(asset_list_json);
    printf("  [OK] Asset list hides 'none' account and masks 'mask_balance'/'mask_account_no'\n");

    // 6.2 Asset detail: asset_b returns 1003, asset_a masks balance/transactions/weight
    csilk_json_t* detail_b = NULL;
    assert(asset_usecase_get(pool, member_id, member_id, asset_b, &detail_b) == 1003 && detail_b == NULL);
    csilk_json_t* detail_a = NULL;
    assert(asset_usecase_get(pool, member_id, member_id, asset_a, &detail_a) == 0 && detail_a != NULL);
    assert(csilk_json_get_bool(detail_a, "is_balance_masked") == true);
    assert(db_get_num(detail_a, "current_value") == 0.0);
    assert(db_get_num(detail_a, "portfolio_weight") == 0.0);
    csilk_json_t* dtxs = csilk_json_get(detail_a, "transactions");
    assert(dtxs && csilk_json_array_size(dtxs) >= 1);
    assert(db_get_num(csilk_json_array_get(dtxs, 0), "amount") == 0.0);
    csilk_json_free(detail_a);
    printf("  [OK] Asset detail hides 'none' and masks balance/transactions/portfolio_weight\n");

    // 6.3 Asset balance logs: asset_b hidden, asset_a masked
    csilk_json_t* logs_json = NULL;
    int64_t logs_total = 0;
    query_asset_log_filter_t lfil = {.user_id = member_id, .page = 1, .page_size = 20};
    assert(asset_usecase_query_logs(pool, &lfil, &logs_json, &logs_total) == 0);
    assert(logs_total > 0);
    for (int i = 0; i < (int)csilk_json_array_size(logs_json); i++) {
        csilk_json_t* lrow = csilk_json_array_get(logs_json, i);
        assert(db_get_int(lrow, "asset_id") == asset_a);
        assert(csilk_json_get_bool(lrow, "is_balance_masked") == true);
        assert(db_get_num(lrow, "delta") == 0.0);
        assert(db_get_num(lrow, "balance_after") == 0.0);
    }
    csilk_json_free(logs_json);

    // 6.4 Asset update & delete blocked with 1004 on viewer account
    update_asset_cmd_t uacmd = {.user_id = member_id, .id = asset_a, .name = "篡改名称"};
    assert(asset_usecase_update(pool, &uacmd, &ares) != 0 && ares.code == 1004);
    delete_asset_cmd_t dacmd = {.user_id = member_id, .id = asset_a};
    assert(asset_usecase_delete(pool, &dacmd, &ares) != 0 && ares.code == 1004);
    printf("  [OK] Asset update/delete blocked with 1004 on viewer account\n");

    // 6.5 Transactions list, monthly, update, delete
    query_tx_filter_t tfil = {.user_id = member_id, .ledger_id = ledger_id, .page = 1, .page_size = 20};
    assert(tx_usecase_query(pool, &tfil, &txres) == 0 && txres.data_payload != NULL);
    assert(txres.total == 1);
    csilk_json_t* txitem = csilk_json_array_get((csilk_json_t*)txres.data_payload, 0);
    assert(db_get_int(txitem, "id") == tx_a);
    assert(csilk_json_get_bool(txitem, "is_balance_masked") == true);
    assert(db_get_num(txitem, "amount") == 0.0);
    csilk_json_free((csilk_json_t*)txres.data_payload);

    assert(tx_usecase_monthly(pool, member_id, ledger_id, "2026-11", &txres) == 0 && txres.data_payload != NULL);
    assert(db_get_num((csilk_json_t*)txres.data_payload, "inflows") == 0.0);
    csilk_json_free((csilk_json_t*)txres.data_payload);

    update_tx_cmd_t utxcmd = {.user_id = member_id, .ledger_id = ledger_id, .tx_id = tx_a, .amount = 999.0};
    assert(tx_usecase_update(pool, &utxcmd, &txres) != 0 && txres.code == 1004);
    delete_tx_cmd_t dtxcmd = {.user_id = member_id, .tx_id = tx_a};
    assert(tx_usecase_delete(pool, &dtxcmd, &txres) != 0 && txres.code == 1004);
    printf("  [OK] Transaction list/monthly/update/delete enforce RBAC visibility, masking, and write guard\n");

    // 6.6 Daily expenses list, monthly, update, delete
    csilk_json_t* delist = NULL;
    int64_t detotal = 0;
    assert(daily_expense_usecase_list(pool, member_id, ledger_id, 1, 20, NULL, NULL, NULL, NULL, NULL, &delist, &detotal) == 0);
    assert(detotal == 1 && csilk_json_array_size(delist) == 1);
    csilk_json_t* deitem = csilk_json_array_get(delist, 0);
    assert(db_get_int(deitem, "id") == exp_a);
    assert(csilk_json_get_bool(deitem, "is_balance_masked") == true);
    assert(db_get_num(deitem, "amount") == 0.0);
    csilk_json_free(delist);

    daily_expense_monthly_result_t demres = {0};
    csilk_json_t *de_cat = NULL, *de_tag = NULL, *de_daily = NULL;
    assert(daily_expense_usecase_monthly(pool, member_id, ledger_id, 2026, 11, &demres, &de_cat, &de_tag, &de_daily) == 0);
    assert(money_to_double(demres.total_expense) == 0.0);
    if (de_cat) csilk_json_free(de_cat);
    if (de_tag) csilk_json_free(de_tag);
    if (de_daily) csilk_json_free(de_daily);

    update_daily_expense_cmd_t udecmd = {
        .user_id = member_id,
        .ledger_id = ledger_id,
        .id = exp_a,
        .category_id = exp_cat_id,
        .asset_id = asset_a,
        .expense_type = "expense",
        .amount = 888.0,
        .currency = "CNY",
        .expense_date = "2026-11-20",
    };
    assert(daily_expense_usecase_update(pool, &udecmd, NULL, &deres) != 0 && deres.code == 1004);
    assert(daily_expense_usecase_delete(pool, member_id, ledger_id, exp_a, &deres) != 0 && deres.code == 1004);
    printf("  [OK] Daily expense list/monthly/update/delete enforce RBAC visibility, masking, and write guard\n");

    // 6.7 Reports, portfolio recent transactions, and AI repos exclude masked/none balances
    report_query_t rq = {.user_id = member_id, .ledger_id = ledger_id};
    report_result_t rres = {0};
    report_asset_summary(pool, &rq, &rres);
    assert(rres.code == 0 && rres.data_payload != NULL);
    assert(db_get_num(rres.data_payload, "total_assets") == 0.0);
    csilk_json_free(rres.data_payload);

    csilk_json_t* rec_txs = portfolio_repo_get_recent_transactions(pool, member_id, ledger_id, 5);
    assert(rec_txs != NULL && csilk_json_array_size(rec_txs) == 1);
    assert(db_get_num(csilk_json_array_get(rec_txs, 0), "amount") == 0.0);
    assert(csilk_json_get_bool(csilk_json_array_get(rec_txs, 0), "is_balance_masked") == true);
    csilk_json_free(rec_txs);

    int64_t ai_total = 0;
    csilk_json_t* ai_assets = mf_ai_repo_asset_list(pool, member_id, 1, 20, NULL, &ai_total);
    assert(ai_assets != NULL && ai_total == 1);
    assert(db_get_num(csilk_json_array_get(ai_assets, 0), "current_value") == 0.0);
    csilk_json_free(ai_assets);

    csilk_json_t* ai_exps = mf_ai_repo_daily_expense_list(pool, member_id, 1, 20, NULL, NULL, 0, NULL, &ai_total);
    assert(ai_exps != NULL && ai_total == 1);
    assert(db_get_num(csilk_json_array_get(ai_exps, 0), "amount") == 0.0);
    csilk_json_free(ai_exps);
    printf("  [OK] Reports, portfolio, and AI repositories enforce RBAC masking & exclusion\n");
}

static void
test_csv_export_rbac_and_shared_ledger(csilk_db_pool_t* pool)
{
    printf("--- Testing CSV Export Shared Ledger & Account RBAC ---\n");
    int64_t owner_id = 0, member_id = 0;
    assert(mf_auth_repo_create(pool, "exp_owner", "hash1", &owner_id) == 0 && owner_id > 0);
    assert(mf_auth_repo_create(pool, "exp_member", "hash2", &member_id) == 0 && member_id > 0);

    char s_oid[32], s_mid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_mid, sizeof(s_mid), "%lld", (long long)member_id);

    csilk_json_t* lrow = csilk_db_query_param_json(
        pool,
        "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'ExportShared', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    assert(lrow && csilk_json_array_size(lrow) == 1);
    int64_t ledger_id = db_get_int(csilk_json_array_get(lrow, 0), "id");
    csilk_json_free(lrow);

    char s_lid[32];
    snprintf(s_lid, sizeof(s_lid), "%lld", (long long)ledger_id);
    csilk_db_exec_param(
        pool,
        "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'viewer');",
        (const char*[]){s_lid, s_oid, s_lid, s_mid, NULL});

    // Seed categories for owner
    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES (?, ?, 'ExportCash', 'asset', 'cash');",
        (const char*[]){s_oid, s_lid, NULL});
    csilk_json_t* crow = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id=? AND ledger_id=? AND name='ExportCash';",
        (const char*[]){s_oid, s_lid, NULL});
    int64_t cash_cat = db_get_int(csilk_json_array_get(crow, 0), "id");
    csilk_json_free(crow);

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type) VALUES (?, ?, 'ExportFood', 'expense');",
        (const char*[]){s_oid, s_lid, NULL});
    crow = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id=? AND ledger_id=? AND name='ExportFood';",
        (const char*[]){s_oid, s_lid, NULL});
    int64_t exp_cat = db_get_int(csilk_json_array_get(crow, 0), "id");
    csilk_json_free(crow);

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_open = {.user_id = owner_id, .ledger_id = ledger_id, .name = "ExpOpen", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_mask = {.user_id = owner_id, .ledger_id = ledger_id, .name = "ExpMask", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_none = {.user_id = owner_id, .ledger_id = ledger_id, .name = "ExpNone", .institution = "bank_card", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_open, &acres) == 0);
    int64_t acct_open = acres.id;
    assert(account_usecase_create(pool, &ac_mask, &acres) == 0);
    int64_t acct_mask = acres.id;
    assert(account_usecase_create(pool, &ac_none, &acres) == 0);
    int64_t acct_none = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t as_open = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cash_cat, .account_id = acct_open, .name = "OpenCash", .current_value = 1000.0, .currency = "CNY"};
    create_asset_cmd_t as_mask = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cash_cat, .account_id = acct_mask, .name = "MaskCash", .current_value = 2000.0, .currency = "CNY"};
    create_asset_cmd_t as_none = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cash_cat, .account_id = acct_none, .name = "NoneCash", .current_value = 3000.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &as_open, &ares) == 0);
    int64_t asset_open = ares.id;
    assert(asset_usecase_create(pool, &as_mask, &ares) == 0);
    int64_t asset_mask = ares.id;
    assert(asset_usecase_create(pool, &as_none, &ares) == 0);
    int64_t asset_none = ares.id;

    tx_usecase_result_t txres = {0};
    create_tx_cmd_t tx_open = {.user_id = owner_id, .ledger_id = ledger_id, .asset_id = asset_open, .category_id = exp_cat, .type = "income", .amount = 111.0, .currency = "CNY", .date = "2026-11-21", .note = "open_tx"};
    create_tx_cmd_t tx_mask = {.user_id = owner_id, .ledger_id = ledger_id, .asset_id = asset_mask, .category_id = exp_cat, .type = "income", .amount = 222.0, .currency = "CNY", .date = "2026-11-21", .note = "mask_tx"};
    create_tx_cmd_t tx_none = {.user_id = owner_id, .ledger_id = ledger_id, .asset_id = asset_none, .category_id = exp_cat, .type = "income", .amount = 333.0, .currency = "CNY", .date = "2026-11-21", .note = "none_tx"};
    create_tx_cmd_t tx_cross = {.user_id = owner_id, .ledger_id = ledger_id, .asset_id = asset_open, .linked_asset_id = asset_mask, .category_id = exp_cat, .type = "income", .amount = 444.0, .currency = "CNY", .date = "2026-11-21", .note = "linked_mask_tx"};
    assert(tx_usecase_create(pool, &tx_open, &txres) == 0);
    assert(tx_usecase_create(pool, &tx_mask, &txres) == 0);
    assert(tx_usecase_create(pool, &tx_none, &txres) == 0);
    assert(tx_usecase_create(pool, &tx_cross, &txres) == 0);

    daily_expense_usecase_result_t deres = {0};
    create_daily_expense_cmd_t de_open = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = exp_cat, .asset_id = asset_open, .expense_type = "expense", .amount = 15.0, .currency = "CNY", .expense_date = "2026-11-21", .note = "open_exp"};
    create_daily_expense_cmd_t de_mask = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = exp_cat, .asset_id = asset_mask, .expense_type = "expense", .amount = 25.0, .currency = "CNY", .expense_date = "2026-11-21", .note = "mask_exp"};
    create_daily_expense_cmd_t de_none = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = exp_cat, .asset_id = asset_none, .expense_type = "expense", .amount = 35.0, .currency = "CNY", .expense_date = "2026-11-21", .note = "none_exp"};
    assert(daily_expense_usecase_create(pool, &de_open, NULL, &deres) == 0);
    assert(daily_expense_usecase_create(pool, &de_mask, NULL, &deres) == 0);
    assert(daily_expense_usecase_create(pool, &de_none, NULL, &deres) == 0);

    // Restrict member: mask on acct_mask, none on acct_none
    assert(account_permission_usecase_set(pool, owner_id, ledger_id, acct_mask, member_id, "viewer", 1, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, ledger_id, acct_none, member_id, "none", 0, 0, &acres) == 0);

    // Member exports: sees ONLY open_tx and open_exp (owner rows shared, masked/none excluded)
    csilk_json_t* tx_rows = export_query_transactions_rows(pool, member_id, ledger_id);
    assert(tx_rows != NULL && csilk_json_array_size(tx_rows) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(tx_rows, 0), "note"), "open_tx") == 0);
    csilk_json_free(tx_rows);

    csilk_json_t* de_rows = export_query_daily_expenses_rows(pool, member_id, ledger_id);
    assert(de_rows != NULL && csilk_json_array_size(de_rows) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(de_rows, 0), "note"), "open_exp") == 0);
    csilk_json_free(de_rows);

    // Owner still exports all 4 tx and 3 expenses
    tx_rows = export_query_transactions_rows(pool, owner_id, ledger_id);
    assert(tx_rows != NULL && csilk_json_array_size(tx_rows) == 4);
    csilk_json_free(tx_rows);

    de_rows = export_query_daily_expenses_rows(pool, owner_id, ledger_id);
    assert(de_rows != NULL && csilk_json_array_size(de_rows) == 3);
    csilk_json_free(de_rows);
    printf("  [OK] CSV export shares owner rows and excludes masked/none accounts\n");
}

static void
test_csv_import_rbac_and_isolation(csilk_db_pool_t* pool)
{
    printf("--- Testing CSV Import Ledger Isolation, Anti-Enumeration & RBAC ---\n");
    int64_t owner_id = 0, member_id = 0;
    assert(mf_auth_repo_create(pool, "imp_owner", "hash1", &owner_id) == 0 && owner_id > 0);
    assert(mf_auth_repo_create(pool, "imp_member", "hash2", &member_id) == 0 && member_id > 0);

    char s_oid[32], s_mid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_mid, sizeof(s_mid), "%lld", (long long)member_id);

    csilk_json_t* l1 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'ImpL1', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid1 = db_get_int(csilk_json_array_get(l1, 0), "id");
    csilk_json_free(l1);

    csilk_json_t* l2 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'ImpL2', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid2 = db_get_int(csilk_json_array_get(l2, 0), "id");
    csilk_json_free(l2);

    char s_l1[32], s_l2[32];
    snprintf(s_l1, sizeof(s_l1), "%lld", (long long)lid1);
    snprintf(s_l2, sizeof(s_l2), "%lld", (long long)lid2);
    csilk_db_exec_param(
        pool,
        "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'editor'), (?, ?, 'owner');",
        (const char*[]){s_l1, s_oid, s_l1, s_mid, s_l2, s_oid, NULL});

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES "
        "(?, ?, 'ImpCash', 'asset', 'cash'), (?, ?, 'ImpCash', 'asset', 'cash');",
        (const char*[]){s_oid, s_l1, s_oid, s_l2, NULL});
    csilk_json_t* c1 = csilk_db_query_param_json(
        pool, "SELECT id FROM categories WHERE user_id=? AND ledger_id=? AND name='ImpCash';",
        (const char*[]){s_oid, s_l1, NULL});
    int64_t cat1 = db_get_int(csilk_json_array_get(c1, 0), "id");
    csilk_json_free(c1);
    csilk_json_t* c2 = csilk_db_query_param_json(
        pool, "SELECT id FROM categories WHERE user_id=? AND ledger_id=? AND name='ImpCash';",
        (const char*[]){s_oid, s_l2, NULL});
    int64_t cat2 = db_get_int(csilk_json_array_get(c2, 0), "id");
    csilk_json_free(c2);

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type) VALUES (?, ?, 'ImpFood', 'expense');",
        (const char*[]){s_oid, s_l1, NULL});

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_l1_op = {.user_id = owner_id, .ledger_id = lid1, .name = "L1Op", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_l1_vw = {.user_id = owner_id, .ledger_id = lid1, .name = "L1Vw", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_l1_no = {.user_id = owner_id, .ledger_id = lid1, .name = "L1No", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_l2_op = {.user_id = owner_id, .ledger_id = lid2, .name = "L2Op", .institution = "bank_card", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_l1_op, &acres) == 0); int64_t acct_op = acres.id;
    assert(account_usecase_create(pool, &ac_l1_vw, &acres) == 0); int64_t acct_vw = acres.id;
    assert(account_usecase_create(pool, &ac_l1_no, &acres) == 0); int64_t acct_no = acres.id;
    assert(account_usecase_create(pool, &ac_l2_op, &acres) == 0); int64_t acct_l2 = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t as_l1_op = {.user_id = owner_id, .ledger_id = lid1, .category_id = cat1, .account_id = acct_op, .name = "SharedWallet", .current_value = 1000.0, .currency = "CNY"};
    create_asset_cmd_t as_l2_dup = {.user_id = owner_id, .ledger_id = lid2, .category_id = cat2, .account_id = acct_l2, .name = "SameNameInL2", .current_value = 5000.0, .currency = "CNY"};
    create_asset_cmd_t as_l1_vw = {.user_id = owner_id, .ledger_id = lid1, .category_id = cat1, .account_id = acct_vw, .name = "ReadonlyWallet", .current_value = 2000.0, .currency = "CNY"};
    create_asset_cmd_t as_l1_no = {.user_id = owner_id, .ledger_id = lid1, .category_id = cat1, .account_id = acct_no, .name = "HiddenSecret", .current_value = 9999.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &as_l1_op, &ares) == 0); int64_t asset_op = ares.id;
    assert(asset_usecase_create(pool, &as_l2_dup, &ares) == 0); int64_t asset_l2 = ares.id;
    assert(asset_usecase_create(pool, &as_l1_vw, &ares) == 0); int64_t asset_vw = ares.id;
    assert(asset_usecase_create(pool, &as_l1_no, &ares) == 0);

    // Permissions on lid1 for member_id: operator on acct_op, viewer on acct_vw, none on acct_no
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_op, member_id, "operator", 0, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_vw, member_id, "viewer", 0, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_no, member_id, "none", 0, 0, &acres) == 0);

    // Seed future snapshot at 2026-11-25 on asset_op to verify backdated CSV expense invalidates it
    ledger_position_state_t dummy_pos = {0};
    money_t zero_m = money_zero(CURRENCY_CNY);
    assert(ledger_snapshot_save(pool, owner_id, asset_op, "2026-11-25", &dummy_pos, zero_m, "2026-11-25", 1, "[]") == 0);

    char* out_json = NULL;
    char  err_buf[256] = {0};

    // 0. Oversized payload (> 10MB) rejected before parsing
    assert(import_transactions_csv_execute(pool, 0, owner_id, lid1, "h\n", MF_MAX_CSV_IMPORT_BYTES + 1, &out_json, err_buf, sizeof(err_buf)) < 0);
    assert(import_daily_expenses_csv_execute(pool, 0, owner_id, lid1, "h\n", MF_MAX_CSV_IMPORT_BYTES + 1, &out_json, err_buf, sizeof(err_buf)) < 0);

    // 1. Transactions CSV import by member into lid1:
    //   row1: SharedWallet valid -> OK
    //   row2: SameNameInL2 (belongs to lid2, even for owner!) -> fail ("找不到资产")
    //   row3: HiddenSecret (none) -> fail ("找不到资产", NOT "无权限")
    //   row4: ReadonlyWallet (viewer) -> fail ("账户无记账权限")
    //   row5: SharedWallet with unknown linked_asset_name -> fail ("找不到关联资产")
    const char* tx_csv =
        "date,asset_name,category_name,transaction_type,source_type,amount,price_per_unit,quantity,fee,currency,linked_asset_name,note\n"
        "2026-11-20,SharedWallet,ImpFood,income,income,100,0,0,0,CNY,,ok_row\n"
        "2026-11-20,SameNameInL2,ImpFood,income,income,50,0,0,0,CNY,,cross_ledger\n"
        "2026-11-20,HiddenSecret,ImpFood,income,income,50,0,0,0,CNY,,hidden_probe\n"
        "2026-11-20,ReadonlyWallet,ImpFood,income,income,50,0,0,0,CNY,,viewer_blocked\n"
        "2026-11-20,SharedWallet,ImpFood,income,income,50,0,0,0,CNY,NoSuchLinked,bad_link\n";
    out_json = NULL;
    assert(import_transactions_csv_execute(pool, 0, member_id, lid1, tx_csv, strlen(tx_csv), &out_json, err_buf, sizeof(err_buf)) == 0);
    assert(out_json != NULL);
    csilk_json_t* res_obj = csilk_json_parse(out_json);
    assert(res_obj != NULL);
    assert(db_get_int(res_obj, "imported") == 1);
    assert(db_get_int(res_obj, "errors") == 4);
    const char* details = csilk_json_get_string(res_obj, "errors_detail");
    assert(details != NULL);
    assert(strstr(details, "找不到资产 'SameNameInL2'") != NULL);
    assert(strstr(details, "找不到资产 'HiddenSecret'") != NULL);
    assert(strstr(details, "账户无记账权限") != NULL);
    assert(strstr(details, "找不到关联资产 'NoSuchLinked'") != NULL);
    csilk_json_free(res_obj);
    free(out_json);

    // Owner tries importing into lid1 referencing lid2's SameNameInL2 -> must ALSO fail (no cross-ledger hijack)
    const char* owner_cross_csv =
        "date,asset_name,category_name,transaction_type,source_type,amount,price_per_unit,quantity,fee,currency,linked_asset_name,note\n"
        "2026-11-20,SameNameInL2,ImpFood,income,income,50,0,0,0,CNY,,cross_ledger\n";
    out_json = NULL;
    assert(import_transactions_csv_execute(pool, 0, owner_id, lid1, owner_cross_csv, strlen(owner_cross_csv), &out_json, err_buf, sizeof(err_buf)) == 0);
    res_obj = csilk_json_parse(out_json);
    assert(db_get_int(res_obj, "imported") == 0 && db_get_int(res_obj, "errors") == 1);
    csilk_json_free(res_obj);
    free(out_json);

    // Verify lid2 asset balance stayed 5000.0 untouched
    char s_al2[32];
    snprintf(s_al2, sizeof(s_al2), "%lld", (long long)asset_l2);
    csilk_json_t* chk_l2 = csilk_db_query_param_json(
        pool, "SELECT current_value FROM assets WHERE id=?", (const char*[]){s_al2, NULL});
    assert(db_get_num(csilk_json_array_get(chk_l2, 0), "current_value") == 5000.0);
    csilk_json_free(chk_l2);

    // 2. Daily expenses CSV import: valid row invalidates backdated snapshot; viewer/none rejected
    const char* de_csv =
        "date,asset_name,category_name,expense_type,amount,currency,note\n"
        "2026-11-20,SharedWallet,ImpFood,expense,30,CNY,ok_exp\n"
        "2026-11-20,ReadonlyWallet,ImpFood,expense,40,CNY,vw_exp\n"
        "2026-11-20,HiddenSecret,ImpFood,expense,50,CNY,no_exp\n";
    out_json = NULL;
    assert(import_daily_expenses_csv_execute(pool, 0, member_id, lid1, de_csv, strlen(de_csv), &out_json, err_buf, sizeof(err_buf)) == 0);
    res_obj = csilk_json_parse(out_json);
    assert(db_get_int(res_obj, "imported") == 1);
    assert(db_get_int(res_obj, "errors") == 2);
    details = csilk_json_get_string(res_obj, "errors_detail");
    assert(strstr(details, "找不到资产 'HiddenSecret'") != NULL);
    assert(strstr(details, "账户无记账权限") != NULL);
    csilk_json_free(res_obj);
    free(out_json);

    // Snapshot on 2026-11-25 must have been invalidated by 2026-11-20 daily expense import
    csilk_json_t* snap = NULL;
    assert(ledger_snapshot_get_latest(pool, owner_id, asset_op, NULL, &snap) == 0 && snap == NULL);

    // ReadonlyWallet balance untouched (2000.0)
    char s_avw[32];
    snprintf(s_avw, sizeof(s_avw), "%lld", (long long)asset_vw);
    csilk_json_t* chk_vw = csilk_db_query_param_json(
        pool, "SELECT current_value FROM assets WHERE id=?", (const char*[]){s_avw, NULL});
    assert(db_get_num(csilk_json_array_get(chk_vw, 0), "current_value") == 2000.0);
    csilk_json_free(chk_vw);
    printf("  [OK] CSV import enforces ledger isolation, anti-enumeration, RBAC write guard & snapshot invalidation\n");
}

static void
test_portfolio_shared_ledger_rbac(csilk_db_pool_t* pool)
{
    printf("--- Testing Portfolio Repository Shared Ledger + RBAC Alignment ---\n");
    int64_t owner_id = 0, member_id = 0;
    assert(mf_auth_repo_create(pool, "port_owner", "hash1", &owner_id) == 0 && owner_id > 0);
    assert(mf_auth_repo_create(pool, "port_member", "hash2", &member_id) == 0 && member_id > 0);

    char s_oid[32], s_mid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_mid, sizeof(s_mid), "%lld", (long long)member_id);

    csilk_json_t* lrow = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'PortShared', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t ledger_id = db_get_int(csilk_json_array_get(lrow, 0), "id");
    csilk_json_free(lrow);

    char s_lid[32];
    snprintf(s_lid, sizeof(s_lid), "%lld", (long long)ledger_id);
    csilk_db_exec_param(
        pool, "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'viewer');",
        (const char*[]){s_lid, s_oid, s_lid, s_mid, NULL});

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES "
        "(?, ?, 'PortCash', 'asset', 'cash'), "
        "(?, ?, 'PortStock', 'asset', 'stock'), "
        "(?, ?, 'PortLoan', 'asset', 'loan');",
        (const char*[]){s_oid, s_lid, s_oid, s_lid, s_oid, s_lid, NULL});
    csilk_json_t* cr = csilk_db_query_param_json(
        pool, "SELECT id, asset_type FROM categories WHERE user_id=? AND ledger_id=?;",
        (const char*[]){s_oid, s_lid, NULL});
    int64_t cat_cash = 0, cat_stock = 0, cat_loan = 0;
    for (size_t i = 0; i < csilk_json_array_size(cr); i++) {
        csilk_json_t* r = csilk_json_array_get(cr, i);
        const char*   at = csilk_json_get_string(r, "asset_type");
        if (strcmp(at, "cash") == 0) cat_cash = db_get_int(r, "id");
        if (strcmp(at, "stock") == 0) cat_stock = db_get_int(r, "id");
        if (strcmp(at, "loan") == 0) cat_loan = db_get_int(r, "id");
    }
    csilk_json_free(cr);

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_open = {.user_id = owner_id, .ledger_id = ledger_id, .name = "PortOpen", .institution = "broker", .currency = "CNY"};
    create_account_cmd_t ac_mask = {.user_id = owner_id, .ledger_id = ledger_id, .name = "PortMask", .institution = "broker", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_open, &acres) == 0); int64_t acct_open = acres.id;
    assert(account_usecase_create(pool, &ac_mask, &acres) == 0); int64_t acct_mask = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t c_cash = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cat_cash, .account_id = acct_open, .name = "OwnerCash", .current_value = 10000.0, .currency = "CNY"};
    create_asset_cmd_t c_stk = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cat_stock, .account_id = acct_open, .name = "OwnerStock", .current_value = 0.0, .quantity = 0.0, .cost_basis = 0.0, .net_value = 10.0, .currency = "CNY"};
    create_asset_cmd_t c_loan = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cat_loan, .account_id = acct_open, .name = "OwnerLoan", .current_value = 500.0, .currency = "CNY"};
    create_asset_cmd_t c_mstk = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cat_stock, .account_id = acct_mask, .name = "OwnerMaskStock", .current_value = 9000.0, .quantity = 90.0, .cost_basis = 8000.0, .net_value = 100.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &c_cash, &ares) == 0); int64_t asset_cash = ares.id;
    assert(asset_usecase_create(pool, &c_stk, &ares) == 0); int64_t asset_stk = ares.id;
    assert(asset_usecase_create(pool, &c_loan, &ares) == 0);
    assert(asset_usecase_create(pool, &c_mstk, &ares) == 0);

    tx_usecase_result_t txres = {0};
    create_tx_cmd_t buy_cmd = {
        .user_id = owner_id, .ledger_id = ledger_id, .asset_id = asset_stk, .linked_asset_id = asset_cash,
        .category_id = cat_stock, .type = "buy", .amount = 1000.0, .price = 10.0, .quantity = 100.0,
        .currency = "CNY", .date = "2026-11-22", .note = "owner_buy"
    };
    assert(tx_usecase_create(pool, &buy_cmd, &txres) == 0);

    // Restrict member on acct_mask (mask_balance=1); acct_open remains unmasked viewer
    assert(account_permission_usecase_set(pool, owner_id, ledger_id, acct_mask, member_id, "viewer", 1, 0, &acres) == 0);

    mf_holding_item_t* items = NULL; size_t hcount = 0;
    assert(mf_portfolio_repo_get_holdings(pool, member_id, ledger_id, &items, &hcount) == 0);
    assert(hcount == 1 && items[0].asset_id == asset_stk);
    mf_portfolio_repo_free_holdings(items, hcount);

    mf_portfolio_trade_event_t* evs = NULL; size_t ecount = 0;
    assert(mf_portfolio_repo_get_trade_events(pool, member_id, ledger_id, &evs, &ecount) == 0);
    assert(ecount == 1 && evs[0].asset_id == asset_stk);
    mf_portfolio_repo_free_trade_events(evs, ecount);

    csilk_json_t* cat_assets = portfolio_repo_get_category_assets(pool, member_id, ledger_id);
    assert(cat_assets && csilk_json_array_size(cat_assets) == 2); // OwnerCash + OwnerStock, OwnerMaskStock excluded
    csilk_json_free(cat_assets);

    csilk_json_t* liabs = portfolio_repo_get_liability_assets(pool, member_id, ledger_id);
    assert(liabs && csilk_json_array_size(liabs) == 1);
    csilk_json_free(liabs);

    csilk_json_t* rec = portfolio_repo_get_recent_transactions(pool, member_id, ledger_id, 10);
    assert(rec && csilk_json_array_size(rec) == 1);
    csilk_json_free(rec);

    csilk_json_t* trend_a = portfolio_repo_get_trend_assets(pool, member_id, ledger_id);
    assert(trend_a && csilk_json_array_size(trend_a) == 3);
    csilk_json_free(trend_a);

    csilk_json_t* trend_l = portfolio_repo_get_trend_balance_logs(pool, member_id, ledger_id, "2026-11-01");
    assert(trend_l && csilk_json_array_size(trend_l) >= 1);
    csilk_json_free(trend_l);

    csilk_json_t* perf_t = portfolio_repo_get_performance_transactions(pool, member_id, ledger_id);
    assert(perf_t && csilk_json_array_size(perf_t) == 1);
    csilk_json_free(perf_t);

    csilk_json_t* hraw = portfolio_repo_get_holdings_raw(pool, member_id, ledger_id);
    assert(hraw && csilk_json_array_size(hraw) == 1);
    csilk_json_free(hraw);
    printf("  [OK] All 9 portfolio repo queries support shared ledger members + RBAC exclusion\n");
}

static void
test_dca_shared_ledger_and_rbac(csilk_db_pool_t* pool)
{
    printf("--- Testing DCA Ledger Isolation, Shared Access & Account RBAC ---\n");
    int64_t owner_id = 0, editor_id = 0, viewer_id = 0;
    assert(mf_auth_repo_create(pool, "dca_owner", "h1", &owner_id) == 0);
    assert(mf_auth_repo_create(pool, "dca_editor", "h2", &editor_id) == 0);
    assert(mf_auth_repo_create(pool, "dca_viewer", "h3", &viewer_id) == 0);

    char s_oid[32], s_eid[32], s_vid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_eid, sizeof(s_eid), "%lld", (long long)editor_id);
    snprintf(s_vid, sizeof(s_vid), "%lld", (long long)viewer_id);

    csilk_json_t* lr1 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'DcaL1', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid1 = db_get_int(csilk_json_array_get(lr1, 0), "id");
    csilk_json_free(lr1);
    csilk_json_t* lr2 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'DcaL2', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid2 = db_get_int(csilk_json_array_get(lr2, 0), "id");
    csilk_json_free(lr2);

    char s_l1[32], s_l2[32];
    snprintf(s_l1, sizeof(s_l1), "%lld", (long long)lid1);
    snprintf(s_l2, sizeof(s_l2), "%lld", (long long)lid2);
    csilk_db_exec_param(
        pool,
        "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'editor'), (?, ?, 'viewer'), (?, ?, 'owner');",
        (const char*[]){s_l1, s_oid, s_l1, s_eid, s_l1, s_vid, s_l2, s_oid, NULL});

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES "
        "(?, ?, 'DcaCash1', 'asset', 'cash'), (?, ?, 'DcaFund1', 'asset', 'fund'), (?, ?, 'DcaCash2', 'asset', 'cash');",
        (const char*[]){s_oid, s_l1, s_oid, s_l1, s_oid, s_l2, NULL});
    csilk_json_t* cr = csilk_db_query_param_json(
        pool, "SELECT id, name FROM categories WHERE user_id=?;", (const char*[]){s_oid, NULL});
    int64_t c_cash1 = 0, c_fund1 = 0, c_cash2 = 0;
    for (size_t i = 0; i < csilk_json_array_size(cr); i++) {
        csilk_json_t* r = csilk_json_array_get(cr, i);
        const char*   nm = csilk_json_get_string(r, "name");
        if (strcmp(nm, "DcaCash1") == 0) c_cash1 = db_get_int(r, "id");
        if (strcmp(nm, "DcaFund1") == 0) c_fund1 = db_get_int(r, "id");
        if (strcmp(nm, "DcaCash2") == 0) c_cash2 = db_get_int(r, "id");
    }
    csilk_json_free(cr);

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_open = {.user_id = owner_id, .ledger_id = lid1, .name = "DcaOpen", .institution = "fund_platform", .currency = "CNY"};
    create_account_cmd_t ac_mask = {.user_id = owner_id, .ledger_id = lid1, .name = "DcaMask", .institution = "fund_platform", .currency = "CNY"};
    create_account_cmd_t ac_none = {.user_id = owner_id, .ledger_id = lid1, .name = "DcaNone", .institution = "fund_platform", .currency = "CNY"};
    create_account_cmd_t ac_l2   = {.user_id = owner_id, .ledger_id = lid2, .name = "DcaL2", .institution = "bank_card", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_open, &acres) == 0); int64_t acct_open = acres.id;
    assert(account_usecase_create(pool, &ac_mask, &acres) == 0); int64_t acct_mask = acres.id;
    assert(account_usecase_create(pool, &ac_none, &acres) == 0); int64_t acct_none = acres.id;
    assert(account_usecase_create(pool, &ac_l2, &acres) == 0);   int64_t acct_l2 = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t a_cash1 = {.user_id = owner_id, .ledger_id = lid1, .category_id = c_cash1, .account_id = acct_open, .name = "DcaCashA", .current_value = 5000.0, .currency = "CNY"};
    create_asset_cmd_t a_fund_op = {.user_id = owner_id, .ledger_id = lid1, .category_id = c_fund1, .account_id = acct_open, .name = "DcaFundOpen", .current_value = 1000.0, .quantity = 100.0, .cost_basis = 900.0, .net_value = 10.0, .currency = "CNY"};
    create_asset_cmd_t a_fund_mk = {.user_id = owner_id, .ledger_id = lid1, .category_id = c_fund1, .account_id = acct_mask, .name = "DcaFundMask", .current_value = 8000.0, .quantity = 800.0, .cost_basis = 7000.0, .net_value = 10.0, .currency = "CNY"};
    create_asset_cmd_t a_fund_no = {.user_id = owner_id, .ledger_id = lid1, .category_id = c_fund1, .account_id = acct_none, .name = "DcaFundNone", .current_value = 9000.0, .quantity = 900.0, .cost_basis = 8000.0, .net_value = 10.0, .currency = "CNY"};
    create_asset_cmd_t a_cash2 = {.user_id = owner_id, .ledger_id = lid2, .category_id = c_cash2, .account_id = acct_l2, .name = "DcaCashL2", .current_value = 5000.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &a_cash1, &ares) == 0);   int64_t cash1 = ares.id;
    assert(asset_usecase_create(pool, &a_fund_op, &ares) == 0); int64_t fund_op = ares.id;
    assert(asset_usecase_create(pool, &a_fund_mk, &ares) == 0); int64_t fund_mk = ares.id;
    assert(asset_usecase_create(pool, &a_fund_no, &ares) == 0); int64_t fund_no = ares.id;
    assert(asset_usecase_create(pool, &a_cash2, &ares) == 0);   int64_t cash2 = ares.id;

    // Cross-ledger asset pairing rejected with 1004
    dca_usecase_result_t dres = {0};
    create_dca_plan_cmd_t bad_cross = {.user_id = owner_id, .ledger_id = lid1, .target_asset_id = fund_op, .funding_asset_id = cash2, .name = "Cross", .amount = 100.0};
    assert(dca_usecase_create_plan(pool, &bad_cross, &dres) < 0 && dres.code == 1004);

    // Ledger-level viewer blocked from creating DCA plan even on unmasked account
    create_dca_plan_cmd_t bad_viewer = {.user_id = viewer_id, .ledger_id = lid1, .target_asset_id = fund_op, .funding_asset_id = cash1, .name = "ViewerBlock", .amount = 100.0};
    assert(dca_usecase_create_plan(pool, &bad_viewer, &dres) < 0 && dres.code == 1004);

    // Owner creates 3 plans in lid1
    create_dca_plan_cmd_t p_op = {.user_id = owner_id, .ledger_id = lid1, .target_asset_id = fund_op, .funding_asset_id = cash1, .name = "PlanOpen", .amount = 100.0};
    create_dca_plan_cmd_t p_mk = {.user_id = owner_id, .ledger_id = lid1, .target_asset_id = fund_mk, .funding_asset_id = cash1, .name = "PlanMask", .amount = 200.0};
    create_dca_plan_cmd_t p_no = {.user_id = owner_id, .ledger_id = lid1, .target_asset_id = fund_no, .funding_asset_id = cash1, .name = "PlanNone", .amount = 300.0};
    int64_t pid_op = dca_usecase_create_plan(pool, &p_op, &dres); assert(pid_op > 0);
    int64_t pid_mk = dca_usecase_create_plan(pool, &p_mk, &dres); assert(pid_mk > 0);
    int64_t pid_no = dca_usecase_create_plan(pool, &p_no, &dres); assert(pid_no > 0);

    // Simulate legacy NULL ledger_id row on pid_op to prove fallback compatibility
    char s_pop[32];
    snprintf(s_pop, sizeof(s_pop), "%lld", (long long)pid_op);
    csilk_db_exec_param(pool, "UPDATE dca_plans SET ledger_id = NULL WHERE id = ?;", (const char*[]){s_pop, NULL});

    // Grant editor_id: viewer+mask on acct_mask, none on acct_none, viewer on acct_open
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_open, editor_id, "viewer", 0, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_mask, editor_id, "viewer", 1, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_none, editor_id, "none", 0, 0, &acres) == 0);

    int64_t total = 0;
    csilk_json_t* list = dca_usecase_list_plans(pool, editor_id, lid1, 1, 20, NULL, &total);
    assert(list != NULL && total == 2 && csilk_json_array_size(list) == 2); // pid_no hidden
    for (size_t i = 0; i < csilk_json_array_size(list); i++) {
        csilk_json_t* it = csilk_json_array_get(list, i);
        assert(db_get_int(it, "id") != pid_no);
        if (db_get_int(it, "id") == pid_mk) {
            assert(csilk_json_get_bool(it, "is_balance_masked") == true);
            assert(db_get_num(it, "target_current_value") == 0.0);
            assert(db_get_num(it, "target_cost_basis") == 0.0);
        }
    }
    csilk_json_free(list);

    // Summary excludes both masked and none (only PlanOpen = 1000.0)
    csilk_json_t* sum = dca_usecase_plan_summary(pool, editor_id, lid1);
    assert(sum != NULL && db_get_num(sum, "total_current_value") == 1000.0);
    csilk_json_free(sum);

    // Create pending execution on pid_op, verify editor_id with viewer account perm is blocked 1004 on confirm & skip & delete
    int64_t ex_id = mf_dca_repo_execution_create(pool, pid_op, owner_id, "2026-11-22", 100.0);
    assert(ex_id > 0);
    confirm_dca_execution_cmd_t ccmd = {.user_id = editor_id, .ledger_id = lid1, .exec_id = ex_id, .actual_amount = 100.0, .executed_price = 10.0};
    dca_confirm_result_t cres = {0};
    assert(dca_usecase_confirm_execution(pool, &ccmd, &cres) != 0 && cres.code == 1004);
    assert(dca_usecase_skip_execution(pool, editor_id, lid1, ex_id, &dres) != 0 && dres.code == 1004);
    assert(dca_usecase_delete_plan(pool, editor_id, lid1, pid_op, &dres) != 0 && dres.code == 1004);

    // In lid2 pid_op is invisible
    total = 0;
    list = dca_usecase_list_plans(pool, owner_id, lid2, 1, 20, NULL, &total);
    assert(list != NULL && total == 0);
    csilk_json_free(list);
    printf("  [OK] DCA enforces ledger isolation, NULL fallback, shared access, masking & WRITE_TX\n");
}

static void
test_cashflow_shared_ledger_and_rbac(csilk_db_pool_t* pool)
{
    printf("--- Testing Cashflow Ledger Isolation, Shared Access & Account RBAC ---\n");
    int64_t owner_id = 0, editor_id = 0;
    assert(mf_auth_repo_create(pool, "cf_owner", "h1", &owner_id) == 0);
    assert(mf_auth_repo_create(pool, "cf_editor", "h2", &editor_id) == 0);

    char s_oid[32], s_eid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_eid, sizeof(s_eid), "%lld", (long long)editor_id);

    csilk_json_t* lr1 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'CfL1', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid1 = db_get_int(csilk_json_array_get(lr1, 0), "id");
    csilk_json_free(lr1);
    csilk_json_t* lr2 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'CfL2', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid2 = db_get_int(csilk_json_array_get(lr2, 0), "id");
    csilk_json_free(lr2);

    char s_l1[32], s_l2[32];
    snprintf(s_l1, sizeof(s_l1), "%lld", (long long)lid1);
    snprintf(s_l2, sizeof(s_l2), "%lld", (long long)lid2);
    csilk_db_exec_param(
        pool, "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'editor'), (?, ?, 'owner');",
        (const char*[]){s_l1, s_oid, s_l1, s_eid, s_l2, s_oid, NULL});

    csilk_db_exec_param(
        pool, "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES (?, ?, 'CfCash1', 'asset', 'cash'), (?, ?, 'CfCash2', 'asset', 'cash');",
        (const char*[]){s_oid, s_l1, s_oid, s_l2, NULL});
    csilk_json_t* cr = csilk_db_query_param_json(
        pool, "SELECT id, name FROM categories WHERE user_id=?;", (const char*[]){s_oid, NULL});
    int64_t c1 = 0, c2 = 0;
    for (size_t i = 0; i < csilk_json_array_size(cr); i++) {
        csilk_json_t* r = csilk_json_array_get(cr, i);
        if (strcmp(csilk_json_get_string(r, "name"), "CfCash1") == 0) c1 = db_get_int(r, "id");
        if (strcmp(csilk_json_get_string(r, "name"), "CfCash2") == 0) c2 = db_get_int(r, "id");
    }
    csilk_json_free(cr);

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_op = {.user_id = owner_id, .ledger_id = lid1, .name = "CfOpen", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_mk = {.user_id = owner_id, .ledger_id = lid1, .name = "CfMask", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_no = {.user_id = owner_id, .ledger_id = lid1, .name = "CfNone", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_l2 = {.user_id = owner_id, .ledger_id = lid2, .name = "CfL2", .institution = "bank_card", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_op, &acres) == 0); int64_t acct_op = acres.id;
    assert(account_usecase_create(pool, &ac_mk, &acres) == 0); int64_t acct_mk = acres.id;
    assert(account_usecase_create(pool, &ac_no, &acres) == 0); int64_t acct_no = acres.id;
    assert(account_usecase_create(pool, &ac_l2, &acres) == 0); int64_t acct_l2 = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t as_src = {.user_id = owner_id, .ledger_id = lid1, .category_id = c1, .account_id = acct_op, .name = "CfSrc", .current_value = 10000.0, .currency = "CNY"};
    create_asset_cmd_t as_op  = {.user_id = owner_id, .ledger_id = lid1, .category_id = c1, .account_id = acct_op, .name = "CfTgtOpen", .current_value = 100.0, .currency = "CNY"};
    create_asset_cmd_t as_mk  = {.user_id = owner_id, .ledger_id = lid1, .category_id = c1, .account_id = acct_mk, .name = "CfTgtMask", .current_value = 200.0, .currency = "CNY"};
    create_asset_cmd_t as_no  = {.user_id = owner_id, .ledger_id = lid1, .category_id = c1, .account_id = acct_no, .name = "CfTgtNone", .current_value = 300.0, .currency = "CNY"};
    create_asset_cmd_t as_l2  = {.user_id = owner_id, .ledger_id = lid2, .category_id = c2, .account_id = acct_l2, .name = "CfL2", .current_value = 400.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &as_src, &ares) == 0); int64_t src_id = ares.id;
    assert(asset_usecase_create(pool, &as_op, &ares) == 0);  int64_t op_id = ares.id;
    assert(asset_usecase_create(pool, &as_mk, &ares) == 0);  int64_t mk_id = ares.id;
    assert(asset_usecase_create(pool, &as_no, &ares) == 0);  int64_t no_id = ares.id;
    assert(asset_usecase_create(pool, &as_l2, &ares) == 0);  int64_t l2_id = ares.id;

    cashflow_usecase_result_t cfres = {0};
    create_cashflow_cmd_t bad_cross = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = l2_id, .name = "Bad", .flow_type = "dividend", .frequency = "monthly", .start_date = "2026-11-01", .expected_amount = 50.0};
    int64_t tmp_id = 0;
    assert(cashflow_usecase_create_schedule(pool, &bad_cross, &tmp_id, &cfres) != 0 && cfres.code == 1004);

    create_cashflow_cmd_t cf_op = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = op_id, .name = "OpenDiv", .flow_type = "dividend", .frequency = "monthly", .start_date = "2026-11-01", .expected_amount = 50.0};
    create_cashflow_cmd_t cf_mk = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = mk_id, .name = "MaskDiv", .flow_type = "dividend", .frequency = "monthly", .start_date = "2026-11-01", .expected_amount = 60.0};
    create_cashflow_cmd_t cf_no = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = no_id, .name = "NoneDiv", .flow_type = "dividend", .frequency = "monthly", .start_date = "2026-11-01", .expected_amount = 70.0};
    int64_t sid_op = 0, sid_mk = 0, sid_no = 0;
    assert(cashflow_usecase_create_schedule(pool, &cf_op, &sid_op, &cfres) == 0);
    assert(cashflow_usecase_create_schedule(pool, &cf_mk, &sid_mk, &cfres) == 0);
    assert(cashflow_usecase_create_schedule(pool, &cf_no, &sid_no, &cfres) == 0);

    // Owner confirms income on open & masked accounts in 2026-11
    int64_t tx_id = 0;
    confirm_cashflow_cmd_t inc_op = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = op_id, .amount = 50.0, .date = "2026-11-15", .name = "OpenInc"};
    confirm_cashflow_cmd_t inc_mk = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = mk_id, .amount = 60.0, .date = "2026-11-15", .name = "MaskInc"};
    assert(cashflow_usecase_confirm_income(pool, &inc_op, &tx_id, &cfres) == 0);
    assert(cashflow_usecase_confirm_income(pool, &inc_mk, &tx_id, &cfres) == 0);

    // Legacy NULL ledger_id on sid_op still appears under lid1
    char s_sop[32];
    snprintf(s_sop, sizeof(s_sop), "%lld", (long long)sid_op);
    csilk_db_exec_param(pool, "UPDATE cashflow_schedules SET ledger_id = NULL WHERE id = ?;", (const char*[]){s_sop, NULL});

    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_op, editor_id, "viewer", 0, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_mk, editor_id, "viewer", 1, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_no, editor_id, "none", 0, 0, &acres) == 0);

    csilk_json_t* list = NULL;
    assert(cashflow_usecase_list_schedules(pool, editor_id, lid1, &list, &cfres) == 0);
    assert(list != NULL && csilk_json_array_size(list) == 2); // NoneDiv excluded
    assert(db_get_int(csilk_json_array_get(list, 0), "id") != sid_no);
    assert(db_get_int(csilk_json_array_get(list, 1), "id") != sid_no);
    csilk_json_free(list);

    // Calendar actual_total excludes masked account (50.0, NOT 110.0), projected_total excludes NoneDiv
    query_calendar_cmd_t cal_cmd = {.user_id = editor_id, .ledger_id = lid1, .year = 2026, .month = 11};
    csilk_json_t* cal = NULL;
    assert(cashflow_usecase_get_calendar(pool, &cal_cmd, &cal, &cfres) == 0 && cal != NULL);
    assert(db_get_num(cal, "actual_total") == 50.0);
    csilk_json_free(cal);

    // Ledger lid2 sees 0 schedules from lid1
    list = NULL;
    assert(cashflow_usecase_list_schedules(pool, owner_id, lid2, &list, &cfres) == 0);
    assert(list != NULL && csilk_json_array_size(list) == 0);
    csilk_json_free(list);

    // Editor with viewer permission on acct_op cannot delete sid_op (1004)
    assert(cashflow_usecase_delete_schedule(pool, editor_id, lid1, sid_op, &cfres) != 0 && cfres.code == 1004);
    assert(cashflow_usecase_delete_schedule(pool, editor_id, lid1, sid_mk, &cfres) != 0 && cfres.code == 1004);
    printf("  [OK] Cashflow enforces ledger isolation, NULL fallback, shared access, calendar unmasked & delete RBAC\n");
}

static void
test_ai_confirm_shared_ledger_and_rbac(csilk_db_pool_t* pool)
{
    printf("--- Testing AI Action Confirm Account RBAC Enforcement ---\n");
    ai_tool_registry_init();

    int64_t owner_id = 0, editor_id = 0;
    assert(mf_auth_repo_create(pool, "ai_rbac_owner", "h1", &owner_id) == 0);
    assert(mf_auth_repo_create(pool, "ai_rbac_editor", "h2", &editor_id) == 0);

    char s_oid[32], s_eid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_eid, sizeof(s_eid), "%lld", (long long)editor_id);

    csilk_json_t* lr = csilk_db_query_param_json(
        pool,
        "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'AiRbacL', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid = db_get_int(csilk_json_array_get(lr, 0), "id");
    csilk_json_free(lr);

    char s_lid[32];
    snprintf(s_lid, sizeof(s_lid), "%lld", (long long)lid);
    csilk_db_exec_param(
        pool,
        "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'editor');",
        (const char*[]){s_lid, s_oid, s_lid, s_eid, NULL});

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES "
        "(?, ?, 'AiCash', 'asset', 'cash');",
        (const char*[]){s_oid, s_lid, NULL});
    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type) VALUES (?, ?, 'AiExp', 'expense');",
        (const char*[]){s_oid, s_lid, NULL});
    csilk_json_t* cr = csilk_db_query_param_json(
        pool, "SELECT id, name FROM categories WHERE user_id=?;", (const char*[]){s_oid, NULL});
    int64_t cash_cat = 0, exp_cat = 0;
    for (size_t i = 0; i < csilk_json_array_size(cr); i++) {
        csilk_json_t* r = csilk_json_array_get(cr, i);
        if (strcmp(csilk_json_get_string(r, "name"), "AiCash") == 0) cash_cat = db_get_int(r, "id");
        if (strcmp(csilk_json_get_string(r, "name"), "AiExp") == 0) exp_cat = db_get_int(r, "id");
    }
    csilk_json_free(cr);
    assert(cash_cat > 0 && exp_cat > 0);

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_ro = {.user_id = owner_id, .ledger_id = lid, .name = "AiRO", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_rw = {.user_id = owner_id, .ledger_id = lid, .name = "AiRW", .institution = "bank_card", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_ro, &acres) == 0);
    int64_t acct_ro = acres.id;
    assert(account_usecase_create(pool, &ac_rw, &acres) == 0);
    int64_t acct_rw = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t as_ro = {.user_id = owner_id, .ledger_id = lid, .category_id = cash_cat, .account_id = acct_ro, .name = "AiAssetRO", .current_value = 1000.0, .currency = "CNY"};
    create_asset_cmd_t as_rw = {.user_id = owner_id, .ledger_id = lid, .category_id = cash_cat, .account_id = acct_rw, .name = "AiAssetRW", .current_value = 1000.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &as_ro, &ares) == 0);
    int64_t asset_ro = ares.id;
    assert(asset_usecase_create(pool, &as_rw, &ares) == 0);
    int64_t asset_rw = ares.id;

    assert(account_permission_usecase_set(pool, owner_id, lid, acct_ro, editor_id, "viewer", 0, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid, acct_rw, editor_id, "operator", 0, 0, &acres) == 0);

    ai_tool_context_t* ed_ctx = ai_tool_context_create(pool, editor_id, 55, "trace_ai_rbac");
    assert(ed_ctx != NULL);
    csilk_json_add_int(ed_ctx->metadata, "ledger_id", lid);

    /* Expense confirm on viewer account -> 1004 */
    csilk_json_t* ea = csilk_json_object();
    csilk_json_add_number(ea, "amount", 50.0);
    csilk_json_add_string(ea, "type", "expense");
    csilk_json_add_string(ea, "date", "2026-11-20");
    csilk_json_add_number(ea, "category_id", (double)exp_cat);
    csilk_json_add_number(ea, "asset_id", (double)asset_ro);
    csilk_json_add_string(ea, "note", "Forbidden");
    char* et = ai_confirmation_create_bound_token(editor_id, 55, "confirm_proposed_expense", ea, AI_RISK_HIGH, 300);
    csilk_json_add_string(ea, "draft_token", et);
    char* er = ai_tool_dispatch_parsed(ed_ctx, "confirm_proposed_expense", ea);
    csilk_json_t* ep = csilk_json_parse(er);
    assert(ep != NULL && !csilk_json_get_bool(ep, "success") && db_get_int(ep, "code") == 1004);
    csilk_json_free(ep); free(er); free(et); csilk_json_free(ea);

    /* Transfer confirm on viewer account -> 1004 */
    ai_permission_set_user_role(editor_id, AI_USER_ROLE_ADMIN);
    csilk_json_t* ta = csilk_json_object();
    csilk_json_add_number(ta, "amount", 100.0);
    csilk_json_add_number(ta, "from_asset_id", (double)asset_rw);
    csilk_json_add_number(ta, "to_asset_id", (double)asset_ro);
    csilk_json_add_string(ta, "date", "2026-11-20");
    csilk_json_add_string(ta, "note", "Forbidden transfer");
    char* tt = ai_confirmation_create_bound_token(editor_id, 55, "confirm_proposed_transfer", ta, AI_RISK_HIGH, 300);
    csilk_json_add_string(ta, "draft_token", tt);
    char* tr = ai_tool_dispatch_parsed(ed_ctx, "confirm_proposed_transfer", ta);
    csilk_json_t* tp = csilk_json_parse(tr);
    assert(tp != NULL && !csilk_json_get_bool(tp, "success") && db_get_int(tp, "code") == 1004);
    csilk_json_free(tp); free(tr); free(tt); csilk_json_free(ta);

    /* Batch confirm on viewer account -> 1004 */
    csilk_json_t* ba = csilk_json_array();
    csilk_json_t* bi = csilk_json_object();
    csilk_json_add_string(bi, "action_type", "daily_expense");
    csilk_json_add_number(bi, "amount", 30.0);
    csilk_json_add_string(bi, "type", "expense");
    csilk_json_add_number(bi, "category_id", (double)exp_cat);
    csilk_json_add_number(bi, "asset_id", (double)asset_ro);
    csilk_json_add_string(bi, "date", "2026-11-20");
    csilk_json_array_append(ba, bi);
    char* bt = ai_confirmation_create_batch_token(editor_id, 55, ba, AI_RISK_HIGH, 300);
    csilk_json_t* bout = NULL;
    int bcode = 0;
    char berr[128] = {0};
    assert(ai_batch_actions_execute_for_session(pool, editor_id, 55, ba, bt, &bout, &bcode, berr, sizeof(berr)) < 0);
    assert(bcode == 1004);
    free(bt); csilk_json_free(ba);

    ai_tool_context_free(ed_ctx);
    printf("  [OK] AI expense/transfer/batch confirm enforce shared-ledger account RBAC (1004)\n");
}

int main(void) {
    printf("========================================\n");
    printf("Starting Account RBAC & Field Masking Tests\n");
    printf("========================================\n");

    test_domain_rules();

    csilk_db_pool_t* pool = setup_test_db();
    test_repo_crud(pool);
    test_usecases_and_masking(pool);
    test_csv_export_rbac_and_shared_ledger(pool);
    test_csv_import_rbac_and_isolation(pool);
    test_portfolio_shared_ledger_rbac(pool);
    test_dca_shared_ledger_and_rbac(pool);
    test_cashflow_shared_ledger_and_rbac(pool);
    test_ai_confirm_shared_ledger_and_rbac(pool);

    printf("========================================\n");
    printf("All Account RBAC Tests Passed Successfully!\n");
    printf("========================================\n");
    return 0;
}
