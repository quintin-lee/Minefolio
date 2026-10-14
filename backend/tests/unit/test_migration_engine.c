#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csilk/csilk.h"
#include "csilk/drivers/db.h"
#include "csilk/drivers/db/migration.h"

static void
test_checksum_normalization(void)
{
    printf("--- Running Checksum Normalization Test ---\n");

    const char* content_lf = "CREATE TABLE foo (\n    id INTEGER PRIMARY KEY\n);\n";
    const char* content_crlf = "CREATE TABLE foo (\r\n    id INTEGER PRIMARY KEY\r\n);\r\n   ";

    char hash1[65] = {0};
    char hash2[65] = {0};

    assert(csilk_migration_checksum_content(content_lf, strlen(content_lf), hash1) == 0);
    assert(csilk_migration_checksum_content(content_crlf, strlen(content_crlf), hash2) == 0);

    assert(strlen(hash1) == 64);
    assert(strlen(hash2) == 64);
    assert(strcmp(hash1, hash2) == 0);

    /* Different content produces different hash */
    const char* content_diff = "CREATE TABLE bar (\n    id INTEGER PRIMARY KEY\n);\n";
    char hash3[65] = {0};
    assert(csilk_migration_checksum_content(content_diff, strlen(content_diff), hash3) == 0);
    assert(strcmp(hash1, hash3) != 0);

    printf("Checksum normalization passed.\n");
}

static void
test_discovery_and_ordering(void)
{
    printf("--- Running Migration Discovery & Ordering Test ---\n");

    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", ":memory:");
    assert(pool != NULL);

    csilk_migration_engine_t* engine = NULL;
    assert(csilk_migration_engine_new(pool, "sql/migrations", &engine) == 0 && engine != NULL);

    csilk_migration_item_t* items = NULL;
    int                     count = 0;
    assert(csilk_migration_discover(engine, &items, &count) == 0);
    assert(count >= 7);

    assert(items[0].version == 1);
    assert(strcmp(items[0].name, "initial_auth_and_system") == 0);
    assert(strlen(items[0].checksum) == 64);

    for (int i = 0; i < count; i++) {
        assert(items[i].version > 0);
        assert(strlen(items[i].name) > 0);
        assert(strlen(items[i].checksum) == 64);
        if (i > 0) {
            assert(items[i].version > items[i - 1].version);
        }
    }

    free(items);
    csilk_migration_engine_free(engine);
    csilk_db_pool_free(pool);

    printf("Migration discovery & ordering passed.\n");
}

static void
test_concurrent_lock(void)
{
    printf("--- Running Migration Mutex Lock Concurrency Test ---\n");

    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", ":memory:");
    assert(pool != NULL);

    assert(csilk_migration_lock_init(pool) == 0);

    /* Worker 1 acquires lock */
    assert(csilk_migration_lock_acquire(pool, "worker_1", 0) == 0);

    /* Worker 2 fails to acquire lock */
    assert(csilk_migration_lock_acquire(pool, "worker_2", 0) != 0);

    /* Worker 1 releases lock */
    assert(csilk_migration_lock_release(pool, "worker_1") == 0);

    /* Worker 2 now acquires lock successfully */
    assert(csilk_migration_lock_acquire(pool, "worker_2", 0) == 0);
    assert(csilk_migration_lock_release(pool, "worker_2") == 0);

    csilk_db_pool_free(pool);
    printf("Migration mutex lock concurrency passed.\n");
}

static void
test_fresh_apply_and_status(void)
{
    printf("--- Running Fresh Apply, Status & Idempotence Test ---\n");

    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", ":memory:");
    assert(pool != NULL);

    csilk_migration_engine_t* engine = NULL;
    assert(csilk_migration_engine_new(pool, "sql/migrations", &engine) == 0 && engine != NULL);

    /* Status before apply */
    csilk_migration_item_t* items = NULL;
    int                     count = 0;
    assert(csilk_migration_status(engine, &items, &count) == 0);
    assert(count >= 7);
    for (int i = 0; i < count; i++) {
        assert(!items[i].is_applied);
        assert(items[i].execution_time_ms == 0);
    }
    free(items);

    /* Apply all migrations */
    int applied = 0;
    assert(csilk_migration_apply(engine, &applied) == 0);
    assert(applied == count);

    /* Status after apply */
    assert(csilk_migration_status(engine, &items, &count) == 0);
    for (int i = 0; i < count; i++) {
        assert(items[i].is_applied);
        assert(items[i].execution_time_ms >= 0);
    }
    free(items);

    /* Verify core tables were created */
    const char* check_tables[] = {
        "users", "categories", "assets", "transactions", "ledgers", "schema_migrations", NULL
    };
    for (int i = 0; check_tables[i] != NULL; i++) {
        const char* params[] = {check_tables[i], NULL};
        csilk_json_t* res = csilk_db_query_param_json(
            pool, "SELECT 1 FROM sqlite_master WHERE type='table' AND name = ?;", params);
        assert(res != NULL && csilk_json_array_size(res) == 1);
        csilk_json_free(res);
    }

    /* Idempotent second apply */
    int second_applied = 0;
    assert(csilk_migration_apply(engine, &second_applied) == 0);
    assert(second_applied == 0);

    /* Validation passes */
    assert(csilk_migration_validate(engine) == 0);

    csilk_migration_engine_free(engine);
    csilk_db_pool_free(pool);
    printf("Fresh apply, status & idempotence passed.\n");
}

static void
test_tampering_detection(void)
{
    printf("--- Running Tampering Detection Test ---\n");

    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", ":memory:");
    assert(pool != NULL);

    csilk_migration_engine_t* engine = NULL;
    assert(csilk_migration_engine_new(pool, "sql/migrations", &engine) == 0 && engine != NULL);

    int applied = 0;
    assert(csilk_migration_apply(engine, &applied) == 0);
    assert(applied >= 7);
    assert(csilk_migration_validate(engine) == 0);

    /* Tamper with checksum of V001 in schema_migrations */
    assert(csilk_db_exec(
               pool,
               "UPDATE schema_migrations SET checksum = "
               "'0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef' "
               "WHERE version = 1;") == 0);

    /* Validate must detect tampering */
    assert(csilk_migration_validate(engine) != 0);

    /* Apply must refuse to execute when tampering is detected */
    int new_applied = 0;
    assert(csilk_migration_apply(engine, &new_applied) != 0);

    csilk_migration_engine_free(engine);
    csilk_db_pool_free(pool);
    printf("Tampering detection passed.\n");
}

static void
test_auto_baseline_existing_database(void)
{
    printf("--- Running Auto-Baseline on Pre-existing Database Test ---\n");

    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", ":memory:");
    assert(pool != NULL);

    /* Simulate legacy production database: users table exists, schema_migrations does NOT */
    assert(csilk_db_exec(
               pool,
               "CREATE TABLE users (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password TEXT);"
               "INSERT INTO users (id, username, password) VALUES (1, 'prod_admin', 'hashed_pw');"
               "CREATE TABLE ledgers (id INTEGER PRIMARY KEY, owner_id INTEGER, is_default INTEGER);"
               "INSERT INTO ledgers (id, owner_id, is_default) VALUES (10, 1, 1);"
               "CREATE TABLE categories (id INTEGER PRIMARY KEY, user_id INTEGER, ledger_id INTEGER, "
               "name TEXT);"
               "INSERT INTO categories (id, user_id, ledger_id, name) VALUES (20, 1, NULL, 'legacy');"
               "CREATE TABLE assets (id INTEGER PRIMARY KEY, user_id INTEGER, ledger_id INTEGER);"
               "CREATE TABLE transactions (id INTEGER PRIMARY KEY, user_id INTEGER, ledger_id INTEGER);"
               "CREATE TABLE daily_expenses (id INTEGER PRIMARY KEY, user_id INTEGER, ledger_id INTEGER);"
               "CREATE TABLE category_seed_state (user_id INTEGER PRIMARY KEY);"
               "INSERT INTO category_seed_state (user_id) VALUES (1);") == 0);

    csilk_migration_engine_t* engine = NULL;
    assert(csilk_migration_engine_new(pool, "sql/migrations", &engine) == 0 && engine != NULL);
    assert(csilk_migration_engine_set_baseline(engine, 7, "users") == 0);

    /* Apply on existing database triggers auto-baseline */
    int applied = 0;
    assert(csilk_migration_apply(engine, &applied) == 0);

    /* The legacy user and record remain visible after the upgrade. */
    csilk_json_t* cat_res =
        csilk_db_query_json(pool, "SELECT ledger_id FROM categories WHERE id = 20;");
    assert(cat_res != NULL && csilk_json_array_size(cat_res) == 1);
    const csilk_json_t* cat_row = csilk_json_array_get(cat_res, 0);
    assert(csilk_json_as_int64(csilk_json_get(cat_row, "ledger_id"), 0) == 10);
    csilk_json_free(cat_res);

    /* Legacy data is completely preserved */
    csilk_json_t* user_res =
        csilk_db_query_json(pool, "SELECT username FROM users WHERE id = 1;");
    assert(user_res != NULL && csilk_json_array_size(user_res) == 1);
    const csilk_json_t* user_row = csilk_json_array_get(user_res, 0);
    assert(strcmp(csilk_json_string_value(csilk_json_get(user_row, "username")), "prod_admin") == 0);
    csilk_json_free(user_res);

    /* Baseline records are in schema_migrations with execution_time_ms = 0 */
    csilk_migration_item_t* items = NULL;
    int                     count = 0;
    assert(csilk_migration_status(engine, &items, &count) == 0);
    assert(count >= 7);
    for (int i = 0; i < count; i++) {
        if (items[i].version <= 7) {
            assert(items[i].is_applied == true);
            assert(items[i].execution_time_ms == 0);
        }
    }
    free(items);

    /* Validation passes */
    assert(csilk_migration_validate(engine) == 0);

    csilk_migration_engine_free(engine);
    csilk_db_pool_free(pool);
    printf("Auto-baseline on pre-existing database passed.\n");
}

static int
mock_pg_connect(csilk_db_pool_t* pool, const char* dsn)
{
    (void)pool;
    (void)dsn;
    return 0;
}

static int
mock_pg_disconnect(csilk_db_pool_t* pool)
{
    (void)pool;
    return 0;
}

static csilk_db_driver_t s_mock_pg_driver = {
    .name = "postgres",
    .connect = mock_pg_connect,
    .disconnect = mock_pg_disconnect,
};

static void
test_postgres_migration_dialect(void)
{
    printf("--- Running PostgreSQL Migration Dialect Contract Test ---\n");

    csilk_db_register_driver("postgres", &s_mock_pg_driver);
    csilk_db_pool_t* pool = csilk_db_pool_new("postgres", "host=localhost dbname=minefolio");
    assert(pool != NULL);

    csilk_migration_engine_t* engine = NULL;
    assert(csilk_migration_engine_new(pool, "sql/migrations", &engine) == 0 && engine != NULL);

    csilk_migration_item_t* items = NULL;
    int                     count = 0;
    assert(csilk_migration_discover(engine, &items, &count) == 0);
    assert(count >= 7);
    assert(strstr(items[0].filepath, "postgres") != NULL);

    free(items);
    csilk_migration_engine_free(engine);
    csilk_db_pool_free(pool);
    printf("PostgreSQL migration dialect contract passed.\n");
}

int
main(void)
{
    printf("==================================================\n");
    printf("Starting Database Migration Engine Unit Tests\n");
    printf("==================================================\n");

    csilk_db_init();

    test_checksum_normalization();
    test_discovery_and_ordering();
    test_concurrent_lock();
    test_fresh_apply_and_status();
    test_tampering_detection();
    test_auto_baseline_existing_database();
    test_postgres_migration_dialect();

    printf("==================================================\n");
    printf("All Database Migration Engine Tests Passed (100%%)!\n");
    printf("==================================================\n");
    return 0;
}
