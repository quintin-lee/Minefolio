#include "application/account/usecases.h"
#include "application/category/usecases.h"
#include "domain/auth/repository.h"
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

static const char* TEST_DB_PATH = "/tmp/test_mf_account_usecases.db";

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
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_account_usecases_32b!");

    csilk_db_pool_t* pool = NULL;
    int rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);

    rc = db_run_migrations(pool);
    assert(rc == 0);

    return pool;
}

int main(void) {
    printf("=== Starting Account Usecases & Repository Tests ===\n");
    csilk_db_pool_t* pool = setup_test_db();

    int64_t user_id = 0;
    int rc = mf_auth_repo_create(pool, "account_tester", "hash_pw_123456", &user_id);
    assert(rc == 0 && user_id > 0);

    // 1. Seed default ledger
    csilk_db_exec_param(pool, "INSERT INTO ledgers (user_id, name, is_default) VALUES (?, '默认账本', 1);",
                        (const char*[]){"1", NULL});
    int64_t ledger_id = ledger_get_default(pool, user_id);
    assert(ledger_id > 0);

    // 2. Test account_usecase_seed_default
    account_usecase_seed_default(pool, user_id, ledger_id);
    int64_t def_id = account_find_default(pool, user_id, ledger_id);
    assert(def_id > 0);
    printf("  [OK] account_usecase_seed_default created id=%lld\n", (long long)def_id);

    // Idempotent test
    account_usecase_seed_default(pool, user_id, ledger_id);
    int64_t def_id_2 = account_find_default(pool, user_id, ledger_id);
    assert(def_id_2 == def_id);
    printf("  [OK] account_usecase_seed_default is idempotent\n");

    // 3. Test account_usecase_create
    account_usecase_result_t res;

    // Missing user_id
    create_account_cmd_t cmd_fail1 = {.user_id = 0, .name = "招商银行", .institution = "bank_card"};
    rc = account_usecase_create(pool, &cmd_fail1, &res);
    assert(rc != 0 && res.code == 1002);

    // Invalid name
    create_account_cmd_t cmd_fail2 = {.user_id = user_id, .name = "", .institution = "bank_card"};
    rc = account_usecase_create(pool, &cmd_fail2, &res);
    assert(rc != 0 && res.code == 1002);

    // Invalid institution
    create_account_cmd_t cmd_fail3 = {.user_id = user_id, .name = "招行", .institution = "super_bank"};
    rc = account_usecase_create(pool, &cmd_fail3, &res);
    assert(rc != 0 && res.code == 1002);

    // Valid creation
    create_account_cmd_t cmd_ok = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = "招商银行储蓄卡",
        .institution = "bank_card",
        .account_no = "62258888",
        .currency = "CNY",
        .icon = "ph:bank",
        .note = "工资卡",
        .sort_order = 10,
    };
    rc = account_usecase_create(pool, &cmd_ok, &res);
    assert(rc == 0 && res.code == 0 && res.id > 0);
    int64_t acct_id = res.id;
    printf("  [OK] account_usecase_create succeeded id=%lld\n", (long long)acct_id);

    // Duplicate name conflict in same ledger
    rc = account_usecase_create(pool, &cmd_ok, &res);
    assert(rc != 0 && res.code == 1004);
    printf("  [OK] account_usecase_create duplicate name rejected with 1004\n");

    // 4. Test account_usecase_query
    csilk_json_t* list = NULL;
    int64_t total = 0;
    rc = account_usecase_query(pool, user_id, ledger_id, 0, &list, &total);
    assert(rc == 0 && list != NULL);
    assert(total >= 2); // default account + cmb account
    assert(csilk_json_array_size(list) >= 2);
    csilk_json_free(list);
    printf("  [OK] account_usecase_query listed accounts total=%lld\n", (long long)total);

    // 5. Test account_usecase_update
    update_account_cmd_t upd_cmd = {
        .user_id = user_id,
        .id = acct_id,
        .name = "招商银行工资卡",
        .institution = "bank_card",
        .account_no = "62259999",
        .currency = "CNY",
        .icon = "ph:credit-card",
        .note = "主薪资卡",
        .sort_order = 20,
        .has_sort_order = 1,
        .has_archived = 0,
        .archived = 0,
    };
    rc = account_usecase_update(pool, &upd_cmd, &res);
    assert(rc == 0 && res.code == 0);

    // Verify update
    csilk_json_t* got = account_get(pool, user_id, acct_id);
    assert(got != NULL && csilk_json_array_size(got) == 1);
    const csilk_json_t* row = csilk_json_array_get(got, 0);
    assert(strcmp(csilk_json_get_string(row, "name"), "招商银行工资卡") == 0);
    assert(strcmp(csilk_json_get_string(row, "note"), "主薪资卡") == 0);
    csilk_json_free(got);
    printf("  [OK] account_usecase_update updated attributes\n");

    // Update non-existent
    upd_cmd.id = 999999;
    rc = account_usecase_update(pool, &upd_cmd, &res);
    assert(rc != 0 && res.code == 1003);

    // 6. Test account_usecase_archive
    rc = account_usecase_archive(pool, user_id, acct_id, 1, &res);
    assert(rc == 0 && res.code == 0);

    // Query without archived -> should not include acct_id
    rc = account_usecase_query(pool, user_id, ledger_id, 0, &list, &total);
    assert(rc == 0 && list != NULL);
    for (size_t i = 0; i < (size_t)csilk_json_array_size(list); i++) {
        const csilk_json_t* r = csilk_json_array_get(list, i);
        assert((int64_t)db_get_num(r, "id") != acct_id);
    }
    csilk_json_free(list);

    // Query with archived -> should include acct_id
    rc = account_usecase_query(pool, user_id, ledger_id, 1, &list, &total);
    assert(rc == 0 && list != NULL);
    bool found_archived = false;
    for (size_t i = 0; i < (size_t)csilk_json_array_size(list); i++) {
        const csilk_json_t* r = csilk_json_array_get(list, i);
        if ((int64_t)db_get_num(r, "id") == acct_id) {
            found_archived = true;
            assert(db_get_int(r, "archived") == 1);
        }
    }
    assert(found_archived);
    csilk_json_free(list);
    printf("  [OK] account_usecase_archive & query filter verified\n");

    // Unarchive
    rc = account_usecase_archive(pool, user_id, acct_id, 0, &res);
    assert(rc == 0 && res.code == 0);

    // 7. Test account_usecase_delete
    // Seed category and insert an asset under acct_id to test deletion protection
    category_usecase_seed_defaults(pool, user_id, ledger_id);
    csilk_json_t* cats = csilk_db_query_param_json(pool,
        "SELECT id FROM categories WHERE user_id=? LIMIT 1;",
        (const char*[]){"1", NULL});
    assert(cats != NULL && csilk_json_array_size(cats) > 0);
    int64_t cat_id = db_get_int(csilk_json_array_get(cats, 0), "id");
    csilk_json_free(cats);

    char acct_id_str[32], uid_str[32], lid_str[32], cat_str[32];
    snprintf(acct_id_str, sizeof(acct_id_str), "%lld", (long long)acct_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)cat_id);
    int ins_rc = csilk_db_exec_param(pool,
                        "INSERT INTO assets (user_id, category_id, account_id, name, currency, current_value) "
                        "VALUES (?, ?, ?, '储蓄存款', 'CNY', 5000.0);",
                        (const char*[]){uid_str, cat_str, acct_id_str, NULL});
    printf("  ins_rc = %d\n", ins_rc);
    assert(ins_rc == 0);

    delete_account_cmd_t del_cmd = {.user_id = user_id, .id = acct_id};
    rc = account_usecase_delete(pool, &del_cmd, &res);
    assert(rc != 0 && res.code == 1004);
    printf("  [OK] account_usecase_delete rejected when assets present (1004)\n");

    // Delete the asset and delete account again
    csilk_db_exec_param(pool, "DELETE FROM assets WHERE account_id = ?;", (const char*[]){acct_id_str, NULL});
    rc = account_usecase_delete(pool, &del_cmd, &res);
    assert(rc == 0 && res.code == 0);
    printf("  [OK] account_usecase_delete succeeded after removing assets\n");

    // Try deleting again -> 1003
    rc = account_usecase_delete(pool, &del_cmd, &res);
    assert(rc != 0 && res.code == 1003);

    printf("=== Account Usecases & Repository Tests Passed ===\n");
    return 0;
}
