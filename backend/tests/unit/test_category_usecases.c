#include "application/category/usecases.h"
#include "domain/auth/repository.h"
#include "infrastructure/repositories/category_repo_impl.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* TEST_DB_PATH = "/tmp/test_mf_category_usecases.db";

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
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_cat_usecases_32b_long!");

    csilk_db_pool_t* pool = NULL;
    int rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);

    rc = db_run_migrations(pool);
    assert(rc == 0);

    return pool;
}

int main(void) {
    printf("=== Starting Category Usecases & Repository Tests ===\n");
    csilk_db_pool_t* pool = setup_test_db();

    int64_t user_id = 0;
    int rc = mf_auth_repo_create(pool, "category_tester", "hash_cat_123456", &user_id);
    assert(rc == 0 && user_id > 0);

    // Setup ledger
    csilk_db_exec_param(pool, "INSERT INTO ledgers (user_id, name, is_default) VALUES (?, '默认账本', 1);",
                        (const char*[]){"1", NULL});
    int64_t ledger_id = ledger_get_default(pool, user_id);
    assert(ledger_id > 0);

    // 1. Test category_usecase_seed_defaults
    category_usecase_seed_defaults(pool, user_id, ledger_id);

    csilk_json_t* list = NULL;
    rc = category_usecase_list(pool, user_id, ledger_id, NULL, &list);
    assert(rc == 0 && list != NULL);
    size_t seed_count = csilk_json_array_size(list);
    assert(seed_count >= 10);
    csilk_json_free(list);
    printf("  [OK] category_usecase_seed_defaults seeded count=%zu\n", seed_count);

    // 2. Test category_usecase_list by type
    rc = category_usecase_list(pool, user_id, ledger_id, "expense", &list);
    assert(rc == 0 && list != NULL);
    assert(csilk_json_array_size(list) > 0);
    csilk_json_free(list);

    rc = category_usecase_list(pool, user_id, ledger_id, "income", &list);
    assert(rc == 0 && list != NULL);
    assert(csilk_json_array_size(list) > 0);
    csilk_json_free(list);
    printf("  [OK] category_usecase_list type filtering works\n");

    // 3. Test category_usecase_create validation failures
    category_usecase_result_t res;
    int64_t new_id = 0;

    // Invalid name
    create_category_cmd_t cmd_fail1 = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = "",
        .type = "expense",
    };
    rc = category_usecase_create(pool, &cmd_fail1, &new_id, &res);
    assert(rc != 0 && res.code == 1002);

    // Invalid type
    create_category_cmd_t cmd_fail2 = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = "自定义分类",
        .type = "unknown_type",
    };
    rc = category_usecase_create(pool, &cmd_fail2, &new_id, &res);
    assert(rc != 0 && res.code == 1002);

    // 4. Test category_usecase_create success
    create_category_cmd_t cmd_parent = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = "数码数码",
        .parent_id = 0,
        .type = "expense",
        .asset_type = NULL,
        .currency = "CNY",
        .icon = "ph:laptop",
        .sort_order = 99,
    };
    rc = category_usecase_create(pool, &cmd_parent, &new_id, &res);
    assert(rc == 0 && new_id > 0);
    int64_t parent_id = new_id;

    // Create child category
    create_category_cmd_t cmd_child = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = "电脑配件",
        .parent_id = parent_id,
        .type = "expense",
        .asset_type = NULL,
        .currency = "CNY",
        .icon = "ph:cpu",
        .sort_order = 1,
    };
    rc = category_usecase_create(pool, &cmd_child, &new_id, &res);
    assert(rc == 0 && new_id > 0);
    int64_t child_id = new_id;
    printf("  [OK] category_usecase_create parent_id=%lld child_id=%lld\n",
           (long long)parent_id, (long long)child_id);

    // 5. Test category_usecase_children
    csilk_json_t* children = NULL;
    rc = category_usecase_children(pool, user_id, ledger_id, parent_id, &children);
    assert(rc == 0 && children != NULL);
    assert(csilk_json_array_size(children) == 1);
    csilk_json_free(children);
    printf("  [OK] category_usecase_children found child\n");

    // 6. Test category_usecase_delete with children (must be rejected)
    delete_category_cmd_t del_parent = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .category_id = parent_id,
    };
    rc = category_usecase_delete(pool, &del_parent, &res);
    assert(rc != 0 && res.code == 1004);
    printf("  [OK] category_usecase_delete rejected when children exist (1004)\n");

    // 7. Test category_usecase_update
    update_category_cmd_t upd_cmd = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .category_id = child_id,
        .name = "外设与显卡",
        .type = "expense",
        .asset_type = NULL,
        .currency = "CNY",
        .icon = "ph:keyboard",
        .sort_order = 2,
    };
    rc = category_usecase_update(pool, &upd_cmd, &res);
    assert(rc == 0);

    // 8. Test category_usecase_delete child first, then parent
    delete_category_cmd_t del_child = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .category_id = child_id,
    };
    rc = category_usecase_delete(pool, &del_child, &res);
    assert(rc == 0);

    rc = category_usecase_delete(pool, &del_parent, &res);
    assert(rc == 0);
    printf("  [OK] category_usecase_delete leaf categories succeeded\n");

    // Delete non-existent
    del_child.category_id = 999999;
    category_usecase_delete(pool, &del_child, &res);
    assert(res.code == 1003);

    printf("=== Category Usecases & Repository Tests Passed ===\n");
    return 0;
}
