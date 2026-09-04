#include "infrastructure/database/database.h"
#include "infrastructure/database/migration/checksum.h"
#include "infrastructure/database/migration/lock.h"
#include "infrastructure/database/migration/migration_engine.h"
#include "infrastructure/database/statement.h"
#include "infrastructure/database/transaction.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
test_checksum_normalization(void)
{
    printf("--- Running Checksum Normalization Test ---\n");

    const char* content_lf = "CREATE TABLE foo (\n    id INTEGER PRIMARY KEY\n);\n";
    const char* content_crlf = "CREATE TABLE foo (\r\n    id INTEGER PRIMARY KEY\r\n);\r\n   ";

    char hash1[65] = {0};
    char hash2[65] = {0};

    assert(mf_migration_checksum_content(content_lf, strlen(content_lf), hash1) == 0);
    assert(mf_migration_checksum_content(content_crlf, strlen(content_crlf), hash2) == 0);

    assert(strlen(hash1) == 64);
    assert(strlen(hash2) == 64);
    assert(strcmp(hash1, hash2) == 0);

    /* Different content produces different hash */
    const char* content_diff = "CREATE TABLE bar (\n    id INTEGER PRIMARY KEY\n);\n";
    char hash3[65] = {0};
    assert(mf_migration_checksum_content(content_diff, strlen(content_diff), hash3) == 0);
    assert(strcmp(hash1, hash3) != 0);

    printf("Checksum normalization passed.\n");
}

static void
test_discovery_and_ordering(void)
{
    printf("--- Running Migration Discovery & Ordering Test ---\n");

    mf_db_config_t cfg = {
        .engine = MF_DB_ENGINE_SQLITE,
        .dsn = ":memory:",
        .busy_timeout_ms = 5000,
    };
    mf_db_t* db = NULL;
    assert(mf_db_open(&cfg, &db) == 0 && db != NULL);

    mf_migration_engine_t* engine = NULL;
    assert(mf_migration_engine_new(db, "sql/migrations", &engine) == 0 && engine != NULL);

    mf_migration_item_t* items = NULL;
    int                  count = 0;
    assert(mf_migration_discover(engine, &items, &count) == 0);
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
    mf_migration_engine_free(engine);
    mf_db_close(db);

    printf("Migration discovery & ordering passed.\n");
}

static void
test_concurrent_lock(void)
{
    printf("--- Running Migration Mutex Lock Concurrency Test ---\n");

    mf_db_config_t cfg = {
        .engine = MF_DB_ENGINE_SQLITE,
        .dsn = ":memory:",
        .busy_timeout_ms = 5000,
    };
    mf_db_t* db = NULL;
    assert(mf_db_open(&cfg, &db) == 0 && db != NULL);

    assert(mf_migration_lock_init(db) == 0);

    /* Worker 1 acquires lock */
    assert(mf_migration_lock_acquire(db, "worker_1", 0) == 0);

    /* Worker 2 fails to acquire lock */
    assert(mf_migration_lock_acquire(db, "worker_2", 0) != 0);

    /* Worker 1 releases lock */
    assert(mf_migration_lock_release(db, "worker_1") == 0);

    /* Worker 2 now acquires lock successfully */
    assert(mf_migration_lock_acquire(db, "worker_2", 0) == 0);
    assert(mf_migration_lock_release(db, "worker_2") == 0);

    mf_db_close(db);
    printf("Migration mutex lock concurrency passed.\n");
}

static void
test_fresh_apply_and_status(void)
{
    printf("--- Running Fresh Apply, Status & Idempotence Test ---\n");

    mf_db_config_t cfg = {
        .engine = MF_DB_ENGINE_SQLITE,
        .dsn = ":memory:",
        .busy_timeout_ms = 5000,
    };
    mf_db_t* db = NULL;
    assert(mf_db_open(&cfg, &db) == 0 && db != NULL);

    mf_migration_engine_t* engine = NULL;
    assert(mf_migration_engine_new(db, "sql/migrations", &engine) == 0 && engine != NULL);

    /* Status before apply */
    mf_migration_item_t* items = NULL;
    int                  count = 0;
    assert(mf_migration_status(engine, &items, &count) == 0);
    assert(count >= 7);
    for (int i = 0; i < count; i++) {
        assert(!items[i].is_applied);
        assert(items[i].execution_time_ms == 0);
    }
    free(items);

    /* Apply all migrations */
    int applied = 0;
    assert(mf_migration_apply(engine, &applied) == 0);
    assert(applied == count);

    /* Status after apply */
    assert(mf_migration_status(engine, &items, &count) == 0);
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
        mf_stmt_t* stmt = NULL;
        assert(mf_stmt_prepare(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name = ?;", &stmt) == 0);
        assert(mf_stmt_bind_text(stmt, 1, check_tables[i]) == 0);
        mf_result_t* res = NULL;
        assert(mf_stmt_query(stmt, &res) == 0);
        assert(mf_result_next(res) == true);
        mf_result_free(res);
        mf_stmt_close(stmt);
    }

    /* Idempotent second apply */
    int second_applied = 0;
    assert(mf_migration_apply(engine, &second_applied) == 0);
    assert(second_applied == 0);

    /* Validation passes */
    assert(mf_migration_validate(engine) == 0);

    mf_migration_engine_free(engine);
    mf_db_close(db);
    printf("Fresh apply, status & idempotence passed.\n");
}

static void
test_tampering_detection(void)
{
    printf("--- Running Tampering Detection Test ---\n");

    mf_db_config_t cfg = {
        .engine = MF_DB_ENGINE_SQLITE,
        .dsn = ":memory:",
        .busy_timeout_ms = 5000,
    };
    mf_db_t* db = NULL;
    assert(mf_db_open(&cfg, &db) == 0 && db != NULL);

    mf_migration_engine_t* engine = NULL;
    assert(mf_migration_engine_new(db, "sql/migrations", &engine) == 0 && engine != NULL);

    int applied = 0;
    assert(mf_migration_apply(engine, &applied) == 0);
    assert(applied >= 7);
    assert(mf_migration_validate(engine) == 0);

    /* Tamper with checksum of V001 in schema_migrations */
    assert(mf_db_execute(db,
        "UPDATE schema_migrations SET checksum = '0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef' "
        "WHERE version = 1;") == 0);

    /* Validate must detect tampering */
    assert(mf_migration_validate(engine) != 0);

    /* Apply must refuse to execute when tampering is detected */
    int new_applied = 0;
    assert(mf_migration_apply(engine, &new_applied) != 0);

    mf_migration_engine_free(engine);
    mf_db_close(db);
    printf("Tampering detection passed.\n");
}

static void
test_auto_baseline_existing_database(void)
{
    printf("--- Running Auto-Baseline on Pre-existing Database Test ---\n");

    mf_db_config_t cfg = {
        .engine = MF_DB_ENGINE_SQLITE,
        .dsn = ":memory:",
        .busy_timeout_ms = 5000,
    };
    mf_db_t* db = NULL;
    assert(mf_db_open(&cfg, &db) == 0 && db != NULL);

    /* Simulate legacy production database: users table exists, schema_migrations does NOT */
    assert(mf_db_execute(db,
        "CREATE TABLE users (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password TEXT);"
        "INSERT INTO users (id, username, password) VALUES (1, 'prod_admin', 'hashed_pw');") == 0);

    mf_migration_engine_t* engine = NULL;
    assert(mf_migration_engine_new(db, "sql/migrations", &engine) == 0 && engine != NULL);

    /* Apply on existing database triggers auto-baseline */
    int applied = 0;
    assert(mf_migration_apply(engine, &applied) == 0);

    /* Legacy data is completely preserved */
    mf_stmt_t* stmt = NULL;
    assert(mf_stmt_prepare(db, "SELECT username FROM users WHERE id = 1;", &stmt) == 0);
    mf_result_t* res = NULL;
    assert(mf_stmt_query(stmt, &res) == 0);
    assert(mf_result_next(res) == true);
    assert(strcmp(mf_result_get_text(res, "username"), "prod_admin") == 0);
    mf_result_free(res);
    mf_stmt_close(stmt);

    /* Baseline records are in schema_migrations with execution_time_ms = 0 */
    mf_migration_item_t* items = NULL;
    int                  count = 0;
    assert(mf_migration_status(engine, &items, &count) == 0);
    assert(count >= 7);
    for (int i = 0; i < count; i++) {
        if (items[i].version <= 7) {
            assert(items[i].is_applied == true);
            assert(items[i].execution_time_ms == 0);
        }
    }
    free(items);

    /* Validation passes */
    assert(mf_migration_validate(engine) == 0);

    mf_migration_engine_free(engine);
    mf_db_close(db);
    printf("Auto-baseline on pre-existing database passed.\n");
}

static void
test_postgres_migration_dialect(void)
{
    printf("--- Running PostgreSQL Migration Dialect Contract Test ---\n");

    mf_db_config_t cfg = {
        .engine = MF_DB_ENGINE_POSTGRES,
        .dsn = "host=localhost dbname=minefolio",
    };
    mf_db_t* db = NULL;
    assert(mf_db_open(&cfg, &db) == 0 && db != NULL);
    assert(mf_db_get_engine(db) == MF_DB_ENGINE_POSTGRES);

    mf_migration_engine_t* engine = NULL;
    assert(mf_migration_engine_new(db, "sql/migrations", &engine) == 0 && engine != NULL);

    mf_migration_item_t* items = NULL;
    int                  count = 0;
    assert(mf_migration_discover(engine, &items, &count) == 0);
    assert(count >= 7);
    assert(strstr(items[0].filepath, "postgres") != NULL);

    free(items);
    mf_migration_engine_free(engine);
    mf_db_close(db);
    printf("PostgreSQL migration dialect contract passed.\n");
}

int
main(void)
{
    printf("==================================================\n");
    printf("Starting Database Migration Engine Unit Tests\n");
    printf("==================================================\n");

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
