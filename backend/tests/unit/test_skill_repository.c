#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "csilk/csilk.h"
#include "common/db.h"
#include "config/secret.h"
#include "application/skill/usecases.h"
#include "domain/auth/repository.h"
#include "domain/skill/entity.h"
#include "infrastructure/repositories/skill_repo_impl.h"

static const char* TEST_DB_PATH = "/tmp/test_mf_skill_repo.db";

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
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_skill_repos_32bytes_long!");

    csilk_db_pool_t* pool = NULL;
    int rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);

    rc = db_run_migrations(pool);
    assert(rc == 0);

    return pool;
}

int main(void) {
    printf("=== Starting Skill Repository Tests ===\n");
    csilk_db_pool_t* pool = setup_test_db();
    int64_t user_id = 0;
    int rc = mf_auth_repo_create(pool, "alice", "hash_alice_123", &user_id);
    assert(rc == 0 && user_id > 0);

    // 1. Insert skill
    mf_skill_t sk = {0};
    strncpy(sk.name, "financial-auditor", sizeof(sk.name) - 1);
    strncpy(sk.description, "Audits monthly financial balance and cashflow.", sizeof(sk.description) - 1);
    strncpy(sk.category, "finance", sizeof(sk.category) - 1);
    strncpy(sk.tags, "audit,cashflow", sizeof(sk.tags) - 1);
    strncpy(sk.license, "MIT", sizeof(sk.license) - 1);
    sk.enabled = 1;
    sk.instructions_l2 = "## Instructions\nPerform a step-by-step audit.";
    sk.backend = MF_SKILL_BACKEND_LOCAL;
    strncpy(sk.exec_cfg.path, "scripts/audit.sh", sizeof(sk.exec_cfg.path) - 1);
    sk.exec_cfg.timeout_ms = 15000;
    sk.exec_cfg.max_output_bytes = 32768;
    sk.is_builtin = 0;

    int64_t id = skill_repo_insert(pool, user_id, &sk);
    assert(id > 0);
    printf("  [OK] skill_repo_insert id=%lld\n", (long long)id);

    // 2. Get by ID
    csilk_json_t* got = skill_repo_get(pool, user_id, id);
    assert(got != NULL);
    assert(strcmp(csilk_json_get_string(got, "name"), "financial-auditor") == 0);
    assert(strcmp(csilk_json_get_string(got, "category"), "finance") == 0);
    assert(db_get_int(got, "enabled") == 1);
    assert(db_get_int(got, "timeout_ms") == 15000);
    csilk_json_free(got);
    printf("  [OK] skill_repo_get\n");

    // 3. Get by Name
    csilk_json_t* got_name = skill_repo_get_by_name(pool, user_id, "financial-auditor");
    assert(got_name != NULL);
    assert(db_get_int(got_name, "id") == id);
    csilk_json_free(got_name);
    printf("  [OK] skill_repo_get_by_name\n");

    // 4. Duplicate name constraint check
    int64_t dup_id = skill_repo_insert(pool, user_id, &sk);
    assert(dup_id <= 0); // Must fail due to UNIQUE (user_id, name)
    printf("  [OK] duplicate skill name rejected\n");

    // 5. List
    csilk_json_t* list1 = skill_repo_list(pool, user_id, NULL, 0);
    assert(list1 != NULL);
    assert(csilk_json_array_size(list1) == 1);
    csilk_json_free(list1);

    csilk_json_t* list_cat = skill_repo_list(pool, user_id, "finance", 0);
    assert(list_cat != NULL && csilk_json_array_size(list_cat) == 1);
    csilk_json_free(list_cat);

    csilk_json_t* list_other = skill_repo_list(pool, user_id, "other_cat", 0);
    assert(list_other != NULL && csilk_json_array_size(list_other) == 0);
    csilk_json_free(list_other);

    int64_t       sk_total = 0;
    csilk_json_t* sk_p1 = skill_repo_list_paged(pool, user_id, NULL, 0, 1, 1, &sk_total);
    assert(sk_p1 != NULL && sk_total == 1 && csilk_json_array_size(sk_p1) == 1);
    csilk_json_free(sk_p1);
    csilk_json_t* sk_p2 = skill_repo_list_paged(pool, user_id, NULL, 0, 2, 1, &sk_total);
    assert(sk_p2 != NULL && sk_total == 1 && csilk_json_array_size(sk_p2) == 0);
    csilk_json_free(sk_p2);
    printf("  [OK] skill_repo_list with filters and pagination\n");

    // 6. Toggle
    assert(skill_repo_toggle(pool, user_id, id, 0) == 0);
    csilk_json_t* list_en = skill_repo_list(pool, user_id, NULL, 1);
    assert(list_en != NULL && csilk_json_array_size(list_en) == 0);
    csilk_json_free(list_en);

    assert(skill_repo_toggle(pool, user_id, id, 1) == 0);
    list_en = skill_repo_list(pool, user_id, NULL, 1);
    assert(list_en != NULL && csilk_json_array_size(list_en) == 1);
    csilk_json_free(list_en);
    printf("  [OK] skill_repo_toggle\n");

    // 7. Update
    strncpy(sk.description, "Updated description.", sizeof(sk.description) - 1);
    assert(skill_repo_update(pool, user_id, id, &sk) == 0);
    got = skill_repo_get(pool, user_id, id);
    assert(got != NULL);
    assert(strcmp(csilk_json_get_string(got, "description"), "Updated description.") == 0);
    csilk_json_free(got);
    printf("  [OK] skill_repo_update\n");

    // 8. Delete
    assert(skill_repo_delete(pool, user_id, id) == 0);
    got = skill_repo_get(pool, user_id, id);
    assert(got == NULL);
    assert(skill_repo_delete(pool, user_id, id) == -1); // Deleting already deleted id must return -1
    assert(skill_repo_delete(pool, user_id, 999999) == -1);
    assert(skill_repo_update(pool, user_id, 999999, &sk) == -1);
    assert(skill_repo_toggle(pool, user_id, 999999, 1) == -1);
    printf("  [OK] skill_repo_delete and non-existent id guards\n");

    // 9. L3 skill_usecase_test_execute security gates: non-admin rejected (1004), empty/root exec_root rejected (1002)
    mf_skill_t l3_sk = {0};
    snprintf(l3_sk.name, sizeof(l3_sk.name), "l3-sec-skill");
    snprintf(l3_sk.description, sizeof(l3_sk.description), "L3 execution test");
    snprintf(l3_sk.category, sizeof(l3_sk.category), "tools");
    snprintf(l3_sk.exec_cfg.path, sizeof(l3_sk.exec_cfg.path), "/bin/echo");
    l3_sk.exec_cfg.root[0] = '\0';
    l3_sk.enabled = 1;
    l3_sk.backend = MF_SKILL_BACKEND_LOCAL;

    int64_t l3_admin_id = skill_repo_insert(pool, user_id, &l3_sk);
    assert(l3_admin_id > 0);
    csilk_json_t* l3_res = NULL;
    assert(skill_usecase_test_execute(pool, user_id, l3_admin_id, "hi", &l3_res) == 1002);
    assert(l3_res == NULL);

    snprintf(l3_sk.exec_cfg.root, sizeof(l3_sk.exec_cfg.root), "/");
    assert(skill_repo_update(pool, user_id, l3_admin_id, &l3_sk) == 0);
    assert(skill_usecase_test_execute(pool, user_id, l3_admin_id, "hi", &l3_res) == 1002);
    assert(l3_res == NULL);

    int64_t bob_id = 0;
    assert(mf_auth_repo_create(pool, "bob_non_admin", "hash_bob_123", &bob_id) == 0 && bob_id > 0);
    snprintf(l3_sk.exec_cfg.root, sizeof(l3_sk.exec_cfg.root), "/tmp");
    int64_t l3_bob_id = skill_repo_insert(pool, bob_id, &l3_sk);
    assert(l3_bob_id > 0);
    assert(skill_usecase_test_execute(pool, bob_id, l3_bob_id, "hi", &l3_res) == 1004);
    assert(l3_res == NULL);
    printf("  [OK] skill_usecase_test_execute admin and exec_root gates\n");

    printf("=== ALL SKILL REPO TESTS PASSED ===\n");
    unlink(TEST_DB_PATH);
    return 0;
}
