#include "infrastructure/database/database.h"
#include "infrastructure/database/postgres/postgres_adapter.h"
#include "infrastructure/database/statement.h"
#include "infrastructure/database/transaction.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
test_sqlite_crud_and_statement(void)
{
    printf("--- Running SQLite CRUD & Statement Bindings Test ---\n");

    mf_db_config_t cfg = {
        .engine = MF_DB_ENGINE_SQLITE,
        .dsn = ":memory:",
        .busy_timeout_ms = 5000,
    };

    mf_db_t* db = NULL;
    int      rc = mf_db_open(&cfg, &db);
    assert(rc == 0 && db != NULL);
    assert(mf_db_get_engine(db) == MF_DB_ENGINE_SQLITE);

    /* 1. 建表 */
    rc = mf_db_execute(db,
                       "CREATE TABLE test_users ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "name TEXT NOT NULL, "
                       "balance REAL NOT NULL, "
                       "is_active INTEGER NOT NULL, "
                       "note TEXT"
                       ");");
    assert(rc == 0);

    /* 2. 预编译与参数绑定插入 */
    mf_stmt_t* stmt = NULL;
    rc = mf_stmt_prepare(
        db,
        "INSERT INTO test_users (name, balance, is_active, note) VALUES (?, ?, ?, ?);",
        &stmt);
    assert(rc == 0 && stmt != NULL);

    assert(mf_stmt_bind_text(stmt, 1, "Alice") == 0);
    assert(mf_stmt_bind_double(stmt, 2, 8888.50) == 0);
    assert(mf_stmt_bind_bool(stmt, 3, true) == 0);
    assert(mf_stmt_bind_null(stmt, 4) == 0);

    int64_t affected = 0;
    rc = mf_stmt_execute(stmt, &affected);
    assert(rc == 0);
    assert(affected == 1);
    mf_stmt_close(stmt);

    /* 3. 游标查询与类型读取 */
    rc = mf_stmt_prepare(
        db, "SELECT id, name, balance, is_active, note FROM test_users WHERE name = ?;", &stmt);
    assert(rc == 0);
    assert(mf_stmt_bind_text(stmt, 1, "Alice") == 0);

    mf_result_t* res = NULL;
    rc = mf_stmt_query(stmt, &res);
    assert(rc == 0 && res != NULL);

    assert(mf_result_column_count(res) == 5);
    assert(strcmp(mf_result_column_name(res, 1), "name") == 0);

    assert(mf_result_next(res) == true);
    assert(mf_result_get_int64(res, "id") == 1);
    assert(strcmp(mf_result_get_text(res, "name"), "Alice") == 0);
    assert(fabs(mf_result_get_double(res, "balance") - 8888.50) < 0.001);
    assert(mf_result_get_bool(res, "is_active") == true);
    assert(mf_result_is_null(res, "note") == true);

    assert(mf_result_next(res) == false);

    /* 4. JSON 兼容桥接验证 */
    csilk_json_t* json_arr = mf_result_to_json(res);
    assert(json_arr != NULL);
    assert(csilk_json_array_size(json_arr) == 1);
    csilk_json_free(json_arr);

    mf_result_free(res);
    mf_stmt_close(stmt);

    mf_db_close(db);
    printf("  ✅ SQLite CRUD, Statement Bindings, and Result Cursors passed\n");
}

static void
test_sqlite_transactions_and_savepoints(void)
{
    printf("--- Running SQLite Transactions & Savepoints Test ---\n");

    mf_db_config_t cfg = {
        .engine = MF_DB_ENGINE_SQLITE,
        .dsn = ":memory:",
        .busy_timeout_ms = 5000,
    };

    mf_db_t* db = NULL;
    assert(mf_db_open(&cfg, &db) == 0);

    assert(mf_db_execute(
               db, "CREATE TABLE items (id INTEGER PRIMARY KEY AUTOINCREMENT, val TEXT);") == 0);

    /* 1. Transaction Rollback 验证 */
    mf_tx_t* tx = NULL;
    assert(mf_tx_begin(db, &tx) == 0);
    assert(mf_tx_execute(tx, "INSERT INTO items (val) VALUES ('RollbackItem');") == 0);
    assert(mf_tx_rollback(tx) == 0);

    mf_stmt_t* stmt = NULL;
    assert(mf_stmt_prepare(db, "SELECT COUNT(*) AS cnt FROM items WHERE val='RollbackItem';",
                           &stmt) == 0);
    mf_result_t* res = NULL;
    assert(mf_stmt_query(stmt, &res) == 0);
    assert(mf_result_next(res) == true);
    assert(mf_result_get_int64(res, "cnt") == 0);
    mf_result_free(res);
    mf_stmt_close(stmt);

    /* 2. Transaction Commit 验证 */
    assert(mf_tx_begin(db, &tx) == 0);
    assert(mf_tx_execute(tx, "INSERT INTO items (val) VALUES ('CommitItem');") == 0);
    assert(mf_tx_commit(tx) == 0);

    assert(mf_stmt_prepare(db, "SELECT COUNT(*) AS cnt FROM items WHERE val='CommitItem';",
                           &stmt) == 0);
    assert(mf_stmt_query(stmt, &res) == 0);
    assert(mf_result_next(res) == true);
    assert(mf_result_get_int64(res, "cnt") == 1);
    mf_result_free(res);
    mf_stmt_close(stmt);

    /* 3. Savepoint & Rollback to Savepoint 局部回滚验证 */
    assert(mf_tx_begin(db, &tx) == 0);
    assert(mf_tx_execute(tx, "INSERT INTO items (val) VALUES ('Record_A');") == 0);

    assert(mf_tx_savepoint(tx, "sp_after_a") == 0);
    assert(mf_tx_execute(tx, "INSERT INTO items (val) VALUES ('Record_B');") == 0);

    /* 回滚到 sp_after_a，Record_B 应当被抹去，Record_A 应当保留 */
    assert(mf_tx_rollback_to_savepoint(tx, "sp_after_a") == 0);
    assert(mf_tx_release_savepoint(tx, "sp_after_a") == 0);
    assert(mf_tx_commit(tx) == 0);

    /* 验证 Record_A 存在 */
    assert(mf_stmt_prepare(db, "SELECT COUNT(*) AS cnt FROM items WHERE val='Record_A';", &stmt) ==
           0);
    assert(mf_stmt_query(stmt, &res) == 0);
    assert(mf_result_next(res) == true);
    assert(mf_result_get_int64(res, "cnt") == 1);
    mf_result_free(res);
    mf_stmt_close(stmt);

    /* 验证 Record_B 不存在 */
    assert(mf_stmt_prepare(db, "SELECT COUNT(*) AS cnt FROM items WHERE val='Record_B';", &stmt) ==
           0);
    assert(mf_stmt_query(stmt, &res) == 0);
    assert(mf_result_next(res) == true);
    assert(mf_result_get_int64(res, "cnt") == 0);
    mf_result_free(res);
    mf_stmt_close(stmt);

    mf_db_close(db);
    printf("  ✅ SQLite Transactions, Commit, Rollback, and Savepoints passed\n");
}

static void
test_postgres_adapter_and_translation(void)
{
    printf("--- Running PostgreSQL Adapter & Dialect Translation Test ---\n");

    /* 1. 占位符转换测试 */
    char* sql1 = mf_postgres_translate_placeholders("SELECT * FROM t WHERE a = ? AND b = ?;");
    assert(sql1 != NULL);
    assert(strcmp(sql1, "SELECT * FROM t WHERE a = $1 AND b = $2;") == 0);
    free(sql1);

    /* 字符串字面量与注释中的 ? 不应被替换 */
    char* sql2 = mf_postgres_translate_placeholders(
        "INSERT INTO t (col, note) VALUES (?, 'what? question?');");
    assert(sql2 != NULL);
    assert(strcmp(sql2, "INSERT INTO t (col, note) VALUES ($1, 'what? question?');") == 0);
    free(sql2);

    char* sql3 = mf_postgres_translate_placeholders(
        "SELECT ? /* is this ? */ FROM t WHERE name = ?; -- ending ?\n");
    assert(sql3 != NULL);
    assert(strcmp(sql3, "SELECT $1 /* is this ? */ FROM t WHERE name = $2; -- ending ?\n") == 0);
    free(sql3);

    /* 2. PostgreSQL 适配器生命周期与契约测试 */
    mf_db_config_t cfg = {
        .engine = MF_DB_ENGINE_POSTGRES,
        .dsn = "host=localhost dbname=test_mf",
        .busy_timeout_ms = 5000,
    };

    mf_db_t* db = NULL;
    assert(mf_db_open(&cfg, &db) == 0);
    assert(mf_db_get_engine(db) == MF_DB_ENGINE_POSTGRES);

    mf_tx_t* tx = NULL;
    assert(mf_tx_begin(db, &tx) == 0);
    assert(mf_tx_savepoint(tx, "pg_sp1") == 0);
    assert(mf_tx_rollback_to_savepoint(tx, "pg_sp1") == 0);
    assert(mf_tx_release_savepoint(tx, "pg_sp1") == 0);
    assert(mf_tx_commit(tx) == 0);

    /* 语句参数化与预编译 */
    mf_stmt_t* stmt = NULL;
    assert(mf_stmt_prepare(db, "SELECT * FROM users WHERE id = ? AND name = ?;", &stmt) == 0);
    assert(mf_stmt_bind_int64(stmt, 1, 42) == 0);
    assert(mf_stmt_bind_text(stmt, 2, "Bob") == 0);
    int64_t aff = 0;
    assert(mf_stmt_execute(stmt, &aff) == 0);
    mf_stmt_close(stmt);

    mf_db_close(db);
    printf("  ✅ PostgreSQL Adapter Lifecycle, Placeholders, and Savepoint syntax passed\n");
}

int
main(void)
{
    printf("====================================================\n");
    printf("  Starting Dual-Engine Database Repository Test Suite\n");
    printf("====================================================\n");

    test_sqlite_crud_and_statement();
    test_sqlite_transactions_and_savepoints();
    test_postgres_adapter_and_translation();

    printf("====================================================\n");
    printf("  ALL DUAL-ENGINE TESTS PASSED SUCCESSFULLY! (100%%)\n");
    printf("====================================================\n");
    return 0;
}
