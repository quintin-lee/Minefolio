#include "application/tag/usecases.h"
#include "domain/auth/repository.h"
#include "infrastructure/repositories/tag_repo_impl.h"
#include "common/db.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* TEST_DB_PATH = "/tmp/test_mf_tag_usecases.db";

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
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_tag_usecases_32b_long!");

    csilk_db_pool_t* pool = NULL;
    int rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);

    rc = db_run_migrations(pool);
    assert(rc == 0);

    return pool;
}

int main(void) {
    printf("=== Starting Tag Usecases & Repository Tests ===\n");
    csilk_db_pool_t* pool = setup_test_db();

    int64_t user_id = 0;
    int rc = mf_auth_repo_create(pool, "tag_tester", "hash_tag_123456", &user_id);
    assert(rc == 0 && user_id > 0);

    // 1. tag_usecase_create validation failures
    tag_usecase_result_t res;
    int64_t out_id = 0;

    // Empty name
    create_tag_cmd_t cmd_fail1 = {.user_id = user_id, .name = "", .color = "#1890ff"};
    rc = tag_usecase_create(pool, &cmd_fail1, &out_id, &res);
    assert(rc != 0 && res.code == 1002);

    // Invalid color
    create_tag_cmd_t cmd_fail2 = {.user_id = user_id, .name = "餐饮", .color = "blue"};
    rc = tag_usecase_create(pool, &cmd_fail2, &out_id, &res);
    assert(rc != 0 && res.code == 1002);

    // 2. tag_usecase_create success
    create_tag_cmd_t cmd_ok1 = {.user_id = user_id, .name = "餐饮", .color = "#1890ff"};
    rc = tag_usecase_create(pool, &cmd_ok1, &out_id, &res);
    assert(rc == 0 && out_id > 0);
    int64_t tag1_id = out_id;
    printf("  [OK] tag_usecase_create created tag1_id=%lld\n", (long long)tag1_id);

    int64_t tag2_id = 0;
    create_tag_cmd_t cmd_ok2 = {.user_id = user_id, .name = "餐馆打卡", .color = "#52c41a"};
    rc = tag_usecase_create(pool, &cmd_ok2, &tag2_id, &res);
    assert(rc == 0 && tag2_id > 0);

    int64_t tag3_id = 0;
    create_tag_cmd_t cmd_ok3 = {.user_id = user_id, .name = "旅游出行", .color = ""};
    rc = tag_usecase_create(pool, &cmd_ok3, &tag3_id, &res);
    assert(rc == 0 && tag3_id > 0);

    // 3. tag_usecase_list
    csilk_json_t* list = NULL;
    rc = tag_usecase_list(pool, user_id, &list);
    assert(rc == 0 && list != NULL);
    assert(csilk_json_array_size(list) == 3);
    csilk_json_free(list);
    printf("  [OK] tag_usecase_list returned 3 tags\n");

    // 4. tag_usecase_suggestions
    csilk_json_t* sugg = NULL;
    rc = tag_usecase_suggestions(pool, user_id, "餐", &sugg);
    assert(rc == 0 && sugg != NULL);
    assert(csilk_json_array_size(sugg) == 2);
    csilk_json_free(sugg);
    printf("  [OK] tag_usecase_suggestions prefix '餐' matched 2 tags\n");

    // 5. find_by_id
    mf_tag_t tag_ent = {0};
    rc = mf_tag_repo_find_by_id(pool, user_id, tag1_id, &tag_ent);
    assert(rc == 0);
    assert(tag_ent.id == tag1_id);
    assert(strcmp(tag_ent.name, "餐饮") == 0);
    assert(strcmp(tag_ent.color, "#1890ff") == 0);

    rc = mf_tag_repo_find_by_id(pool, user_id, 999999, &tag_ent);
    assert(rc == 1); // not found

    // 6. tag_usecase_update
    update_tag_cmd_t upd_cmd = {
        .user_id = user_id,
        .tag_id = tag1_id,
        .name = "美食餐饮",
        .color = "#ff4d4f",
    };
    rc = tag_usecase_update(pool, &upd_cmd, &res);
    assert(rc == 0);

    rc = mf_tag_repo_find_by_id(pool, user_id, tag1_id, &tag_ent);
    assert(rc == 0);
    assert(strcmp(tag_ent.name, "美食餐饮") == 0);
    assert(strcmp(tag_ent.color, "#ff4d4f") == 0);
    printf("  [OK] tag_usecase_update successfully changed name and color\n");

    // Update non-existent
    upd_cmd.tag_id = 999999;
    tag_usecase_update(pool, &upd_cmd, &res);
    assert(res.code == 1003);

    // 7. tag_usecase_delete
    delete_tag_cmd_t del_cmd = {.user_id = user_id, .tag_id = tag3_id};
    rc = tag_usecase_delete(pool, &del_cmd, &res);
    assert(rc == 0);

    rc = mf_tag_repo_find_by_id(pool, user_id, tag3_id, &tag_ent);
    assert(rc == 1); // deleted

    // Delete non-existent
    del_cmd.tag_id = 999999;
    tag_usecase_delete(pool, &del_cmd, &res);
    assert(res.code == 1003);
    printf("  [OK] tag_usecase_delete verified\n");

    printf("=== Tag Usecases & Repository Tests Passed ===\n");
    return 0;
}
