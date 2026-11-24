#include "application/account/usecases.h"
#include "application/asset/usecases.h"
#include "application/transaction/usecases.h"
#include "application/daily_expense/usecases.h"
#include "domain/account/rules.h"
#include "domain/auth/repository.h"
#include "domain/ai/repository.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "infrastructure/repositories/account_repo_impl.h"
#include "infrastructure/repositories/portfolio_repo_impl.h"
#include "services/report_asset_service.h"
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
