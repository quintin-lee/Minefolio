/** schema_migrations.checksum 自愈子命令的单元测试（不依赖 psql）。 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/db.h"
#include "config/secret.h"
#include "csilk/csilk.h"

static const char* TEST_DB = "/tmp/test_mf_migration_checksum_heal.db";

static void
cleanup_db(void)
{
    unlink(TEST_DB);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s-shm", TEST_DB);
    unlink(buf);
    snprintf(buf, sizeof(buf), "%s-wal", TEST_DB);
    unlink(buf);
}

static csilk_db_pool_t*
setup_db(void)
{
    cleanup_db();
    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", TEST_DB);
    config_secret_set_test_override("JWT_SECRET", "test_migration_heal_jwt_secret_32b!");
    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);
    return pool;
}

int
main(void)
{
    printf("=== Running Migration Checksum Heal Tests ===\n");
    csilk_db_pool_t* pool = setup_db();

    csilk_json_t* rows = csilk_db_query_json(
        pool,
        "SELECT version, name, checksum FROM schema_migrations "
        "WHERE version = 39 LIMIT 1;");
    assert(rows != NULL && csilk_json_is_array(rows) && csilk_json_array_size(rows) == 1);
    const csilk_json_t* row = csilk_json_array_get(rows, 0);
    const char*         name = csilk_json_get_string(row, "name");
    const char*         canonical = csilk_json_get_string(row, "checksum");
    assert(name && name[0] && canonical && canonical[0]);
    char name_buf[256] = {0};
    snprintf(name_buf, sizeof(name_buf), "%s", name);
    char original[65] = {0};
    snprintf(original, sizeof(original), "%s", canonical);
    csilk_json_free(rows);

    /* 漂移的 checksum 应被规范值覆盖，且幂等。 */
    assert(csilk_db_exec(
               pool, "UPDATE schema_migrations SET checksum='deadbeef' WHERE version=39") == 0);
    assert(db_heal_migration_checksums(pool) == 0);
    rows = csilk_db_query_json(
        pool, "SELECT checksum FROM schema_migrations WHERE version=39 LIMIT 1;");
    assert(rows && csilk_json_array_size(rows) == 1);
    const char* healed = csilk_json_get_string(csilk_json_array_get(rows, 0), "checksum");
    assert(healed && strcmp(healed, original) == 0);
    csilk_json_free(rows);
    assert(db_heal_migration_checksums(pool) == 0);
    printf("[Test Heal] PASS: drifted checksum re-seeded and idempotent\n");

    /* 磁盘缺少已记录迁移文件 → fail-fast，不修改库。 */
    assert(csilk_db_exec(
               pool,
               "INSERT INTO schema_migrations (version, name, checksum, applied_at) "
               "VALUES (999, 'no_such_migration', 'x', CURRENT_TIMESTAMP)") == 0);
    assert(db_heal_migration_checksums(pool) != 0);
    rows = csilk_db_query_json(
        pool, "SELECT checksum FROM schema_migrations WHERE version=39 LIMIT 1;");
    assert(rows && csilk_json_array_size(rows) == 1);
    healed = csilk_json_get_string(csilk_json_array_get(rows, 0), "checksum");
    assert(healed && strcmp(healed, original) == 0);
    csilk_json_free(rows);
    printf("[Test Heal] PASS: missing migration file fails fast without mutation\n");

    csilk_db_pool_free(pool);
    cleanup_db();
    printf("ALL MIGRATION CHECKSUM HEAL TESTS PASSED!\n");
    return 0;
}
