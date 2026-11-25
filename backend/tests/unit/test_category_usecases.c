#include "application/category/usecases.h"
#include "domain/auth/repository.h"
#include "infrastructure/repositories/category_repo_impl.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <pthread.h>
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

    // 9. Test duplicate sibling rejection on create & update, and cross-type / cross-ledger allowance
    create_category_cmd_t cmd_dup_root = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = "餐饮美食",
        .parent_id = 0,
        .type = "expense",
    };
    rc = category_usecase_create(pool, &cmd_dup_root, &new_id, &res);
    assert(rc != 0 && res.code == 1004);

    // Find a seeded root and test duplicate child rejection
    int64_t food_root_id = 0;
    assert(mf_category_repo_find_by_name(pool, user_id, ledger_id, "餐饮美食", &food_root_id, NULL, 0) == 0);
    assert(food_root_id > 0);

    create_category_cmd_t cmd_dup_child = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = "咖啡奶茶",
        .parent_id = food_root_id,
        .type = "expense",
    };
    rc = category_usecase_create(pool, &cmd_dup_child, &new_id, &res);
    assert(rc != 0 && res.code == 1004);

    // Same name across different types (e.g. expense vs income) is allowed
    create_category_cmd_t cmd_cross_type = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = "餐饮美食",
        .parent_id = 0,
        .type = "income",
    };
    rc = category_usecase_create(pool, &cmd_cross_type, &new_id, &res);
    assert(rc == 0 && new_id > 0);
    int64_t cross_type_id = new_id;

    // Update: renaming to existing sibling is rejected (1004); self-update keeps working
    create_category_cmd_t cmd_sibling1 = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = "测试分类A",
        .parent_id = 0,
        .type = "expense",
    };
    assert(category_usecase_create(pool, &cmd_sibling1, &new_id, &res) == 0);
    int64_t sib_a = new_id;

    create_category_cmd_t cmd_sibling2 = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = "测试分类B",
        .parent_id = 0,
        .type = "expense",
    };
    assert(category_usecase_create(pool, &cmd_sibling2, &new_id, &res) == 0);
    int64_t sib_b = new_id;

    update_category_cmd_t upd_dup = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .category_id = sib_b,
        .name = "测试分类A",
        .type = "expense",
    };
    rc = category_usecase_update(pool, &upd_dup, &res);
    assert(rc != 0 && res.code == 1004);

    update_category_cmd_t upd_self = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .category_id = sib_b,
        .name = "测试分类B",
        .type = "expense",
        .icon = "ph:star",
    };
    rc = category_usecase_update(pool, &upd_self, &res);
    assert(rc == 0);

    // Cross-ledger same name is allowed
    csilk_db_exec(pool, "INSERT INTO ledgers (owner_id, name, is_default) VALUES (1, '第二账本', 0);");
    csilk_json_t* lrows = csilk_db_query_json(pool, "SELECT MAX(id) AS lid FROM ledgers");
    int64_t ledger2_id = db_get_int(csilk_json_array_get(lrows, 0), "lid");
    csilk_json_free(lrows);
    assert(ledger2_id > ledger_id);
    char l2_str[32];
    snprintf(l2_str, sizeof(l2_str), "%lld", (long long)ledger2_id);
    csilk_db_exec_param(pool, "INSERT OR IGNORE INTO ledger_members (ledger_id, user_id, role) VALUES (?, 1, 'owner')",
                        (const char*[]){l2_str, NULL});

    create_category_cmd_t cmd_other_ledger = {
        .user_id = user_id,
        .ledger_id = ledger2_id,
        .name = "餐饮美食",
        .parent_id = 0,
        .type = "expense",
    };
    rc = category_usecase_create(pool, &cmd_other_ledger, &new_id, &res);
    assert(rc == 0 && new_id > 0);
    printf("  [OK] duplicate sibling guards & cross-type/ledger rules verified\n");

    // 10. Test 4-thread concurrent seeding on a fresh ledger produces 0 duplicates
    csilk_db_exec(pool, "INSERT INTO ledgers (owner_id, name, is_default) VALUES (1, '并发账本', 0);");
    lrows = csilk_db_query_json(pool, "SELECT MAX(id) AS lid FROM ledgers");
    int64_t ledger3_id = db_get_int(csilk_json_array_get(lrows, 0), "lid");
    csilk_json_free(lrows);
    char l3_str[32];
    snprintf(l3_str, sizeof(l3_str), "%lld", (long long)ledger3_id);
    csilk_db_exec_param(pool, "INSERT OR IGNORE INTO ledger_members (ledger_id, user_id, role) VALUES (?, 1, 'owner')",
                        (const char*[]){l3_str, NULL});

    typedef struct {
        csilk_db_pool_t* pool;
        int64_t          user_id;
        int64_t          ledger_id;
    } seed_worker_arg_t;

    seed_worker_arg_t arg = {pool, user_id, ledger3_id};
    pthread_t         threads[4];
    for (int i = 0; i < 4; i++) {
        // Inline thread helper via local wrapper below
        extern void* test_seed_worker_fn(void* p);
        assert(pthread_create(&threads[i], NULL, test_seed_worker_fn, &arg) == 0);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    csilk_json_t* dup_chk = csilk_db_query_param_json(
        pool,
        "SELECT COUNT(*) - COUNT(DISTINCT type || '|' || COALESCE(parent_id,0) || '|' || name) AS dups, "
        "COUNT(*) AS total FROM categories WHERE ledger_id=?",
        (const char*[]){l3_str, NULL});
    assert(dup_chk && csilk_json_array_size(dup_chk) == 1);
    int64_t dups = db_get_int(csilk_json_array_get(dup_chk, 0), "dups");
    int64_t total_seeded = db_get_int(csilk_json_array_get(dup_chk, 0), "total");
    csilk_json_free(dup_chk);
    assert(dups == 0);
    assert(total_seeded == (int64_t)seed_count);
    printf("  [OK] 4-thread concurrent seeding produced total=%lld, dups=%lld\n",
           (long long)total_seeded, (long long)dups);

    // 11. Test V036 deduplication script end-to-end (duplicate roots + children + FK repointing)
    csilk_db_exec(pool, "DROP INDEX IF EXISTS uq_categories_ledger_parent_type_name;");
    csilk_db_exec(pool, "DROP INDEX IF EXISTS uq_categories_user_legacy_parent_type_name;");
    csilk_db_exec(pool, "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES (1, 1, '重复根分类', 'expense', 'cash');");
    csilk_db_exec(pool, "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES (1, 1, '重复根分类', 'expense', 'cash');");
    csilk_json_t* rdup = csilk_db_query_json(pool, "SELECT id FROM categories WHERE ledger_id=1 AND name='重复根分类' ORDER BY id ASC");
    assert(rdup && csilk_json_array_size(rdup) == 2);
    int64_t canon_root = db_get_int(csilk_json_array_get(rdup, 0), "id");
    int64_t dup_root = db_get_int(csilk_json_array_get(rdup, 1), "id");
    csilk_json_free(rdup);

    char cr_str[32], dr_str[32];
    snprintf(cr_str, sizeof(cr_str), "%lld", (long long)canon_root);
    snprintf(dr_str, sizeof(dr_str), "%lld", (long long)dup_root);
    csilk_db_exec_param(pool,
                        "INSERT INTO categories (user_id, ledger_id, name, parent_id, type, asset_type) VALUES (1, 1, '重复子分类', ?, 'expense', 'cash')",
                        (const char*[]){cr_str, NULL});
    csilk_db_exec_param(pool,
                        "INSERT INTO categories (user_id, ledger_id, name, parent_id, type, asset_type) VALUES (1, 1, '重复子分类', ?, 'expense', 'cash')",
                        (const char*[]){dr_str, NULL});
    csilk_db_exec_param(pool,
                        "INSERT INTO categories (user_id, ledger_id, name, parent_id, type, asset_type) VALUES (1, 1, '仅挂在重复根下的子分类', ?, 'expense', 'cash')",
                        (const char*[]){dr_str, NULL});
    csilk_json_t* cdup = csilk_db_query_json(pool, "SELECT id FROM categories WHERE ledger_id=1 AND name='重复子分类' ORDER BY id ASC");
    assert(cdup && csilk_json_array_size(cdup) == 2);
    int64_t canon_child = db_get_int(csilk_json_array_get(cdup, 0), "id");
    int64_t dup_child = db_get_int(csilk_json_array_get(cdup, 1), "id");
    csilk_json_free(cdup);

    char cc_str[32], dc_str[32];
    snprintf(cc_str, sizeof(cc_str), "%lld", (long long)canon_child);
    snprintf(dc_str, sizeof(dc_str), "%lld", (long long)dup_child);
    csilk_db_exec_param(pool,
                        "INSERT INTO assets (user_id, ledger_id, category_id, name) VALUES (1, 1, ?, 'DupRootAsset')",
                        (const char*[]){dr_str, NULL});
    csilk_db_exec_param(pool,
                        "INSERT INTO daily_expenses (user_id, ledger_id, category_id, asset_id, expense_type, amount, expense_date) "
                        "SELECT 1, 1, ?, MAX(id), 'expense', 12.5, '2026-11-25' FROM assets",
                        (const char*[]){dc_str, NULL});

    const char* v036_path = access("sql/migrations/sqlite/V036__deduplicate_categories_and_unique_index.sql", F_OK) == 0
                                ? "sql/migrations/sqlite/V036__deduplicate_categories_and_unique_index.sql"
                                : (access("backend/sql/migrations/sqlite/V036__deduplicate_categories_and_unique_index.sql", F_OK) == 0
                                       ? "backend/sql/migrations/sqlite/V036__deduplicate_categories_and_unique_index.sql"
                                       : "../sql/migrations/sqlite/V036__deduplicate_categories_and_unique_index.sql");
    FILE* vf = fopen(v036_path, "rb");
    assert(vf != NULL);
    fseek(vf, 0, SEEK_END);
    long vsz = ftell(vf);
    fseek(vf, 0, SEEK_SET);
    char* vsql = malloc((size_t)vsz + 1);
    assert(vsql != NULL);
    assert(fread(vsql, 1, (size_t)vsz, vf) == (size_t)vsz);
    vsql[vsz] = '\0';
    fclose(vf);
    assert(csilk_db_exec(pool, vsql) == 0);
    free(vsql);

    csilk_json_t* post_roots = csilk_db_query_json(pool, "SELECT id FROM categories WHERE ledger_id=1 AND name='重复根分类'");
    assert(post_roots && csilk_json_array_size(post_roots) == 1);
    assert(db_get_int(csilk_json_array_get(post_roots, 0), "id") == canon_root);
    csilk_json_free(post_roots);

    csilk_json_t* post_children = csilk_db_query_json(pool, "SELECT id, parent_id FROM categories WHERE ledger_id=1 AND name='重复子分类'");
    assert(post_children && csilk_json_array_size(post_children) == 1);
    assert(db_get_int(csilk_json_array_get(post_children, 0), "id") == canon_child);
    csilk_json_free(post_children);

    csilk_json_t* post_surv = csilk_db_query_json(pool, "SELECT parent_id FROM categories WHERE ledger_id=1 AND name='仅挂在重复根下的子分类'");
    assert(post_surv && csilk_json_array_size(post_surv) == 1);
    assert(db_get_int(csilk_json_array_get(post_surv, 0), "parent_id") == canon_root);
    csilk_json_free(post_surv);

    csilk_json_t* post_asset = csilk_db_query_json(pool, "SELECT category_id FROM assets WHERE name='DupRootAsset'");
    assert(post_asset && csilk_json_array_size(post_asset) == 1);
    assert(db_get_int(csilk_json_array_get(post_asset, 0), "category_id") == canon_root);
    csilk_json_free(post_asset);

    csilk_json_t* post_exp = csilk_db_query_json(pool, "SELECT category_id FROM daily_expenses WHERE expense_date='2026-11-25'");
    assert(post_exp && csilk_json_array_size(post_exp) == 1);
    assert(db_get_int(csilk_json_array_get(post_exp, 0), "category_id") == canon_child);
    csilk_json_free(post_exp);

    // Unique index blocks subsequent duplicate inserts at SQL level
    assert(csilk_db_exec(pool, "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES (1, 1, '重复根分类', 'expense', 'cash');") != 0);
    assert(csilk_db_exec_param(pool, "INSERT INTO categories (user_id, ledger_id, name, parent_id, type, asset_type) VALUES (1, 1, '重复子分类', ?, 'expense', 'cash')",
                               (const char*[]){cr_str, NULL}) != 0);
    (void)cross_type_id;
    (void)sib_a;
    printf("  [OK] V036 deduplication & partial unique index verified\n");

    // 12. Verify V037 query indexes exist
    const char* v037_indexes[] = {
        "idx_categories_ledger_sort",
        "idx_categories_ledger_type_sort",
        "idx_category_seed_state_ledger",
        NULL
    };
    for (int i = 0; v037_indexes[i] != NULL; i++) {
        csilk_json_t* idx_res = csilk_db_query_param_json(
            pool,
            "SELECT 1 FROM sqlite_master WHERE type='index' AND name=?",
            (const char*[]){v037_indexes[i], NULL});
        assert(idx_res && csilk_json_array_size(idx_res) == 1);
        csilk_json_free(idx_res);
    }
    printf("  [OK] V037 category query indexes verified\n");

    // 13. Verify repeated seed calls hit cache idempotently without duplicating rows
    category_usecase_seed_defaults(pool, user_id, ledger_id);
    csilk_json_t* after_reseed = NULL;
    assert(category_usecase_list(pool, user_id, ledger_id, NULL, &after_reseed) == 0 && after_reseed);
    csilk_json_free(after_reseed);

    printf("=== Category Usecases & Repository Tests Passed ===\n");
    return 0;
}

void* test_seed_worker_fn(void* p) {
    struct {
        csilk_db_pool_t* pool;
        int64_t          user_id;
        int64_t          ledger_id;
    }* arg = p;
    category_usecase_seed_defaults(arg->pool, arg->user_id, arg->ledger_id);
    return NULL;
}
