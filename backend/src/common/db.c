/**
 * @file db.c
 * @brief 数据库连接池生命周期、迁移执行及驱动适配实现
 *
 * 实现了 SQLite / PostgreSQL 连接池的创建、运行期 WAL 优化设置、
 * 数据库多版本热迁移（Schema Migration 与数据回填）、以及全局连接池单例维护。
 */

#include "db.h"
#include "config.h"
#include "csilk/csilk.h"
#include "csilk/drivers/db/migration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sqlite3.h>
#include <stdatomic.h>
#include <pthread.h>
#if __has_include(<libpq-fe.h>)
#include <libpq-fe.h>
#endif

/**
 * @brief 内部静态变量：全局数据库连接池单例
 */
static csilk_db_pool_t* g_pool = NULL;

/**
 * @brief 内部静态变量：当前运行的数据库是否为 PostgreSQL (1: PG, 0: SQLite)
 */
static int g_is_postgres = 0;

/**
 * @brief PostgreSQL 连接池的连接数（csilk#6 多连接池）
 *
 * csilk_db_pool_new() 等价于 max_conns=1：所有 csilk_db_with_conn() /
 * acquire() 调用者都只能拿到同一条连接，跨线程时被 mutex 串行化。
 * 对 PostgreSQL 请求显式多连接池，使不同线程可各持一条连接。
 *
 * 不对 SQLite 启用：文件级写锁本就串行，多连接只在只读时有收益，
 * 且 ":memory:" DSN 下每条连接是彼此独立的库。
 *
 * 注意：这条池解决的是「csilk 公共 API 之间的并发」；当前每个 HTTP 请求
 * 仍由同一 worker 线程串行执行 usecase，故并发收益要到请求级并发或
 * 工具级并行（AI runtime 的 pthread worker）真正触库时才体现。
 */
#define MF_DB_DEFAULT_POOL_CONNS 8

/**
 * @brief 初始化全局数据库连接池
 *
 * @param[out] out_pool 接收连接池指针的地址
 * @return int 0 成功，-1 失败
 */
int
db_init(csilk_db_pool_t** out_pool)
{
    csilk_db_init();

    /*
     * 数据库配置完全由环境变量提供 (MINEFOLIO_DB_DRIVER / MINEFOLIO_DB_DSN)。
     * 历史上曾支持从 config/db.json 读取 setup 写入的 driver/dsn，但会引发
     * 容器部署时 env 与残留文件优先级冲突 (已初始化的 PG 部署被误判为未初始化)。
     * 现移除 db.json 读取路径，env 是唯一权威来源；db.json 不再被读写。
     */
    char        drv_buf[64] = {0};
    char        dsn_buf[512] = {0};
    const char* driver_env = config_secret_get("DB_DRIVER", drv_buf, sizeof(drv_buf));
    const char* dsn_env = config_secret_get("DB_DSN", dsn_buf, sizeof(dsn_buf));

    const char* driver = driver_env ? driver_env : "sqlite";
    const char* dsn = dsn_env ? dsn_env
                              : (strcmp(driver, "postgres") == 0
                                     ? "host=localhost user=minefolio dbname=minefolio"
                                     : "./data/minefolio.db");

    g_is_postgres = (strcmp(driver, "postgres") == 0);
    CSILK_LOG_I("DB driver=%s dsn=%s (env-only, config/db.json not consulted)", driver, dsn);

    if (!g_is_postgres) {
        /* Auto-create the data directory if the DSN is a file path */
        char dir[512];
        strncpy(dir, dsn, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        char* slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            mkdir(dir, 0755);
        }
    }
    /* csilk#6: csilk_db_pool_new() is hard-wired to max_conns=1, which
     * serialises every caller (including the parallel tool workers) onto a
     * single connection.  Request an explicit pool for PostgreSQL, where
     * multiple real connections are usable; keep SQLite at 1 because a
     * file-level write lock serialises writes anyway and ":memory:" would
     * give each connection its own private database. */
    int pool_conns = 1;
    if (g_is_postgres) {
        pool_conns = MF_DB_DEFAULT_POOL_CONNS;
    }
    g_pool = csilk_db_pool_new_ex(driver, dsn, pool_conns);
    if (!g_pool) {
        CSILK_LOG_E(
            "Failed to create database pool driver=%s dsn=%s conns=%d", driver, dsn, pool_conns);
        return -1;
    }
    CSILK_LOG_I("Database pool ready: driver=%s conns=%d", driver, pool_conns);

    if (!g_is_postgres) {
        /* csilk#4: apply dialect optimisations through the driver-aware
         * entry point instead of hand-written PRAGMA strings.  This keeps
         * the dialect knowledge inside the driver (a PG pool treats these
         * kinds as no-ops instead of executing SQLite PRAGMA syntax). */
        (void)csilk_db_set_pragma(g_pool, CSILK_DB_PRAGMA_JOURNAL_MODE, "WAL");
        (void)csilk_db_set_pragma(g_pool, CSILK_DB_PRAGMA_BUSY_TIMEOUT, "5000");
        (void)csilk_db_set_pragma(g_pool, CSILK_DB_PRAGMA_SYNCHRONOUS, "NORMAL");
        (void)csilk_db_set_pragma(g_pool, CSILK_DB_PRAGMA_FOREIGN_KEYS, "ON");
        csilk_db_exec(g_pool, "UPDATE ai_traces SET top_p = 0.0 WHERE typeof(top_p) = 'text';");
        csilk_db_exec(g_pool,
                      "UPDATE ai_traces SET temperature = 0.0 WHERE typeof(temperature) = 'text';");

        /* 可观测性：打印实际链接的 SQLite 库版本；RETURNING 语法要求 3.35+ */
        CSILK_LOG_I("SQLite version: %s (min required 3.35 for RETURNING; "
                    "older versions will fail on repository files using RETURNING)",
                    sqlite3_libversion());
    }

    *out_pool = g_pool;
    return 0;
}

static const char*
resolve_migrations_base_dir(void)
{
    static const char* candidates[] = {"sql/migrations",
                                       "backend/sql/migrations",
                                       "../sql/migrations",
                                       "../../sql/migrations",
                                       "../../backend/sql/migrations",
                                       NULL};
    for (int i = 0; candidates[i] != NULL; i++) {
        if (access(candidates[i], R_OK | X_OK) == 0) {
            return candidates[i];
        }
    }
    return "sql/migrations";
}

static void
diagnose_migration_failure(csilk_db_pool_t*          pool,
                           csilk_migration_engine_t* engine,
                           const char*               base_dir)
{
    csilk_migration_item_t* disk_items = NULL;
    int                     disk_count = 0;
    if (csilk_migration_discover(engine, &disk_items, &disk_count) != 0) {
        fprintf(stderr,
                "[migration] Failed to discover migration scripts under '%s' "
                "(missing directory, unreadable file, or duplicate version)\n",
                base_dir ? base_dir : "(null)");
        fflush(stderr);
        return;
    }

    if (csilk_migration_validate(engine) != 0) {
        csilk_json_t* rows = csilk_db_query_json(
            pool, "SELECT version, name, checksum FROM schema_migrations ORDER BY version ASC;");
        if (rows && csilk_json_is_array(rows)) {
            int64_t max_applied = 0;
            bool    printed = false;
            size_t  row_count = csilk_json_array_size(rows);

            for (size_t r = 0; r < row_count; r++) {
                csilk_json_t* row = csilk_json_array_get(rows, r);
                if (!row) {
                    continue;
                }
                int64_t     db_ver = db_get_int(row, "version");
                const char* db_name = csilk_json_get_string(row, "name");
                const char* db_ck = csilk_json_get_string(row, "checksum");
                if (!db_name) {
                    db_name = "";
                }
                if (!db_ck) {
                    db_ck = "";
                }
                if (db_ver > max_applied) {
                    max_applied = db_ver;
                }

                int match_idx = -1;
                for (int i = 0; i < disk_count; i++) {
                    if (disk_items[i].version == (int)db_ver) {
                        match_idx = i;
                        break;
                    }
                }
                if (match_idx < 0) {
                    fprintf(stderr,
                            "[migration] Missing script for applied V%03lld (%s): recorded in "
                            "schema_migrations but not found under '%s'\n",
                            (long long)db_ver,
                            db_name,
                            base_dir ? base_dir : "(null)");
                    printed = true;
                } else if (!db_ck[0] || strcmp(disk_items[match_idx].checksum, db_ck) != 0) {
                    fprintf(stderr,
                            "[migration] Checksum mismatch for V%03lld (%s): db='%s' disk='%s' "
                            "(file=%s; csilk hashes LF-normalized content with trailing whitespace "
                            "stripped)\n",
                            (long long)db_ver,
                            db_name[0] ? db_name : disk_items[match_idx].name,
                            db_ck,
                            disk_items[match_idx].checksum,
                            disk_items[match_idx].filepath);
                    printed = true;
                }
            }

            for (int i = 0; i < disk_count; i++) {
                if (disk_items[i].version >= max_applied) {
                    continue;
                }
                bool applied = false;
                for (size_t r = 0; r < row_count; r++) {
                    csilk_json_t* row = csilk_json_array_get(rows, r);
                    if (row && db_get_int(row, "version") == (int64_t)disk_items[i].version) {
                        applied = true;
                        break;
                    }
                }
                if (!applied) {
                    fprintf(stderr,
                            "[migration] Gap detected: V%03d (%s) is missing from "
                            "schema_migrations while higher version V%03lld is already applied\n",
                            disk_items[i].version,
                            disk_items[i].name,
                            (long long)max_applied);
                    printed = true;
                }
            }

            csilk_json_free(rows);
            if (!printed) {
                fprintf(
                    stderr,
                    "[migration] Validation failed (csilk_migration_validate != 0) under '%s'\n",
                    base_dir ? base_dir : "(null)");
            }
        } else {
            if (rows) {
                csilk_json_free(rows);
            }
            fprintf(stderr,
                    "[migration] Validation failed and schema_migrations could not be queried\n");
        }
        free(disk_items);
        fflush(stderr);
        return;
    }

    free(disk_items);

    csilk_json_t* lock_rows = csilk_db_query_json(
        pool,
        "SELECT is_locked, locked_by, lease_expires_at FROM schema_migration_lock WHERE id = 1;");
    if (lock_rows) {
        if (csilk_json_is_array(lock_rows) && csilk_json_array_size(lock_rows) > 0) {
            csilk_json_t* lrow = csilk_json_array_get(lock_rows, 0);
            if (lrow && db_get_int(lrow, "is_locked") != 0) {
                const char* locked_by = csilk_json_get_string(lrow, "locked_by");
                fprintf(stderr,
                        "[migration] Migration lock is currently held: locked_by='%s' "
                        "lease_expires_at=%lld\n",
                        locked_by ? locked_by : "",
                        (long long)db_get_int(lrow, "lease_expires_at"));
            }
        }
        csilk_json_free(lock_rows);
    }

    csilk_migration_item_t* status_items = NULL;
    int                     status_count = 0;
    if (csilk_migration_status(engine, &status_items, &status_count) == 0) {
        for (int i = 0; i < status_count; i++) {
            if (!status_items[i].is_applied) {
                fprintf(stderr,
                        "[migration] Failed while applying pending migration V%03d (%s, file=%s) "
                        "(see database error logs for exact SQL error)\n",
                        status_items[i].version,
                        status_items[i].name,
                        status_items[i].filepath);
                break;
            }
        }
        free(status_items);
    }

    fflush(stderr);
}

typedef struct {
    const char* dialect;
    int         version;
    const char* legacy_checksum;
    const char* canonical_checksum;
} migration_checksum_alias_t;

static const migration_checksum_alias_t k_known_migration_checksum_aliases[] = {
    {
        .dialect = "postgres",
        .version = 26,
        .legacy_checksum = "c2985d211f8714443548f60161f69a4a305c6504544db2d907a18e5a76b01253",
        .canonical_checksum = "9a01c6efeea743bb4f028884a3046b9092c360b3d3cf29086a902f0e8ed746f6",
    },
    {
        .dialect = "postgres",
        .version = 34,
        .legacy_checksum = "7b378125554f722555f1339caae81384ce4790939049a1a0d8af1f33ba7d79cb",
        .canonical_checksum = "46c4b23533fec31c885fca6fb0f2973a9598748a07efc68d35fdefce99503595",
    },
};

static bool
migration_table_exists(csilk_db_pool_t* pool, const char* actual_driver)
{
    const char* sql = (actual_driver && strcmp(actual_driver, "postgres") == 0)
                          ? "SELECT 1 FROM information_schema.tables "
                            "WHERE table_schema = current_schema() AND table_name = ?;"
                          : "SELECT 1 FROM sqlite_master WHERE type='table' AND name = ?;";

    csilk_stmt_t* stmt = NULL;
    if (csilk_stmt_prepare(pool, sql, &stmt) != 0 || !stmt) {
        return false;
    }
    if (csilk_stmt_bind_text(stmt, 1, "schema_migrations") != 0) {
        csilk_stmt_close(stmt);
        return false;
    }

    csilk_json_t* res = csilk_stmt_query_json(stmt);
    csilk_stmt_close(stmt);

    bool exists = false;
    if (res) {
        if (csilk_json_is_array(res) && csilk_json_array_size(res) > 0) {
            exists = true;
        }
        csilk_json_free(res);
    }
    return exists;
}

int
db_reconcile_known_migration_checksums(csilk_db_pool_t* pool, const char* dialect)
{
    if (!pool) {
        return -1;
    }

    const char* actual_driver = csilk_db_pool_get_driver_name(pool);
    if (!actual_driver || !actual_driver[0]) {
        return 0;
    }
    const char* effective_dialect = (dialect && dialect[0]) ? dialect : actual_driver;

    size_t alias_count = sizeof(k_known_migration_checksum_aliases) /
                         sizeof(k_known_migration_checksum_aliases[0]);
    bool has_alias = false;
    for (size_t i = 0; i < alias_count; i++) {
        if (strcmp(k_known_migration_checksum_aliases[i].dialect, effective_dialect) == 0) {
            has_alias = true;
            break;
        }
    }
    if (!has_alias) {
        return 0;
    }

    if (!migration_table_exists(pool, actual_driver)) {
        return 0;
    }

    int reconciled = 0;
    for (size_t i = 0; i < alias_count; i++) {
        const migration_checksum_alias_t* alias = &k_known_migration_checksum_aliases[i];
        if (strcmp(alias->dialect, effective_dialect) != 0) {
            continue;
        }

        static const char sql[] =
            "UPDATE schema_migrations SET checksum = ? WHERE version = ? AND checksum = ?;";
        csilk_stmt_t* stmt = NULL;
        if (csilk_stmt_prepare(pool, sql, &stmt) != 0 || !stmt) {
            return -1;
        }
        if (csilk_stmt_bind_text(stmt, 1, alias->canonical_checksum) != 0 ||
            csilk_stmt_bind_int64(stmt, 2, alias->version) != 0 ||
            csilk_stmt_bind_text(stmt, 3, alias->legacy_checksum) != 0) {
            csilk_stmt_close(stmt);
            return -1;
        }

        int64_t affected = 0;
        int     rc = csilk_stmt_execute(stmt, &affected);
        csilk_stmt_close(stmt);
        if (rc != 0) {
            return -1;
        }
        if (affected > 0) {
            reconciled += (int)affected;
            CSILK_LOG_I("Reconciled transient %s V%03d checksum (%s -> %s)",
                        effective_dialect,
                        alias->version,
                        alias->legacy_checksum,
                        alias->canonical_checksum);
            fprintf(stderr,
                    "[migration] Reconciled transient V%03d checksum (%s -> %s)\n",
                    alias->version,
                    alias->legacy_checksum,
                    alias->canonical_checksum);
            fflush(stderr);
        }
    }

    return reconciled;
}

/**
 * @brief 执行数据库迁移（通过正式版本化迁移引擎）
 *
 * @param[in] pool 数据库连接池指针
 * @return int 0 成功，-1 失败
 */
int
db_run_migrations(csilk_db_pool_t* pool)
{
    CSILK_LOG_I("Running database migrations (is_postgres=%d)", g_is_postgres);

    const char*               base_dir = resolve_migrations_base_dir();
    csilk_migration_engine_t* engine = NULL;
    if (csilk_migration_engine_new(pool, base_dir, &engine) != 0 || !engine) {
        CSILK_LOG_E("Failed to create migration engine at %s", base_dir);
        fprintf(stderr, "[migration] Failed to create migration engine at '%s'\n", base_dir);
        fflush(stderr);
        return -1;
    }

    /* Auto-baseline versions <= 7 if existing legacy production database (users table exists) */
    csilk_migration_engine_set_baseline(engine, 7, "users");

    /* Reconcile transient 4a2880af PG V026/V034 checksums back to canonical before validation */
    (void)db_reconcile_known_migration_checksums(pool, NULL);

    int applied_count = 0;
    int rc = csilk_migration_apply(engine, &applied_count);
    if (rc != 0) {
        CSILK_LOG_E("Database migration failed");
        diagnose_migration_failure(pool, engine, base_dir);
    } else {
        CSILK_LOG_I("Database migrations finished successfully (%d newly applied)", applied_count);
    }

    csilk_migration_engine_free(engine);
    return rc;
}

/**
 * @brief 获取全局连接池实例指针
 *
 * @return csilk_db_pool_t* 连接池指针
 */
csilk_db_pool_t*
db_get_pool(void)
{
    return g_pool;
}

/**
 * @brief 返回当前是否为 PostgreSQL 数据库模式
 *
 * @return int 1 为 PG，0 为 SQLite
 */
int
db_is_postgres(void)
{
    return g_is_postgres;
}

int
db_wal_checkpoint(csilk_db_pool_t* pool)
{
    if (!pool) {
        return -1;
    }
    if (g_is_postgres) {
        return 0;
    }
    int rc = csilk_db_exec(pool, "PRAGMA wal_checkpoint(TRUNCATE);");
    if (rc != 0) {
        CSILK_LOG_W("db_wal_checkpoint failed, rc=%d", rc);
        return -1;
    }
    CSILK_LOG_I("db_wal_checkpoint(TRUNCATE) succeeded");
    return 0;
}

static pthread_mutex_t  g_tx_mutex;
static pthread_once_t   g_tx_mutex_once = PTHREAD_ONCE_INIT;
static void*            g_tx_pinned_conn = NULL;
static csilk_db_pool_t* g_tx_pinned_pool = NULL;
static int              g_tx_pin_depth = 0;

static void
init_tx_mutex(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_tx_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

typedef struct {
    csilk_db_pool_t* pool;
    const char*      name;
    db_tx_scope_t*   scope;
    int              in_tx;
} tx_with_conn_ctx_t;

static int
in_tx_with_conn_cb(void* conn, void* user_ctx)
{
    (void)conn;
    tx_with_conn_ctx_t* tc = (tx_with_conn_ctx_t*)user_ctx;
    tc->in_tx = csilk_db_in_transaction(tc->pool);
    return 0;
}

static int
tx_begin_with_conn_cb(void* conn, void* user_ctx)
{
    (void)conn;
    tx_with_conn_ctx_t* tc = (tx_with_conn_ctx_t*)user_ctx;
    return csilk_db_tx_scope_begin(tc->pool, tc->name, tc->scope);
}

static void
tx_release_pinned_conn_locked(void)
{
    if (g_tx_pin_depth > 0) {
        g_tx_pin_depth--;
    }
    if (g_tx_pin_depth == 0 && g_tx_pinned_pool && g_tx_pinned_conn) {
        csilk_db_pool_put(g_tx_pinned_pool, g_tx_pinned_conn);
        g_tx_pinned_conn = NULL;
        g_tx_pinned_pool = NULL;
    }
}

int
db_in_transaction(csilk_db_pool_t* pool)
{
    if (!pool) {
        return 0;
    }
    tx_with_conn_ctx_t tc = {.pool = pool, .in_tx = 0};
    if (csilk_db_with_conn(pool, in_tx_with_conn_cb, &tc) != 0) {
        return 0;
    }
    return tc.in_tx;
}

int
db_tx_scope_begin(csilk_db_pool_t* pool, const char* name, db_tx_scope_t* scope)
{
    if (!pool || !scope) {
        return -1;
    }
    pthread_once(&g_tx_mutex_once, init_tx_mutex);
    if (pthread_mutex_lock(&g_tx_mutex) != 0) {
        return -1;
    }

    if (g_tx_pin_depth == 0) {
        void* conn = NULL;
        if (csilk_db_pool_acquire_timeout(pool, &conn, 5000) != 0 || !conn) {
            pthread_mutex_unlock(&g_tx_mutex);
            return -1;
        }
        g_tx_pinned_pool = pool;
        g_tx_pinned_conn = conn;
        g_tx_pin_depth = 1;
    } else if (g_tx_pinned_pool != pool) {
        pthread_mutex_unlock(&g_tx_mutex);
        return -1;
    } else {
        g_tx_pin_depth++;
    }

    tx_with_conn_ctx_t tc = {.pool = pool, .name = name, .scope = scope};
    int                rc = csilk_db_with_conn(pool, tx_begin_with_conn_cb, &tc);
    if (rc != 0) {
        tx_release_pinned_conn_locked();
        pthread_mutex_unlock(&g_tx_mutex);
    }
    return rc;
}

int
db_tx_scope_commit(csilk_db_pool_t* pool, db_tx_scope_t* scope)
{
    bool was_active = (pool != NULL && scope != NULL && scope->active);
    bool is_sp = was_active ? scope->is_savepoint : false;
    char sp_name[64] = {0};
    if (was_active && is_sp) {
        snprintf(sp_name, sizeof(sp_name), "%s", scope->name);
    }

    int rc = csilk_db_tx_scope_commit(pool, scope);
    if (was_active) {
        if (rc != 0) {
            /* COMMIT / RELEASE SAVEPOINT failed and scope->active is now false:
             * ensure the pinned connection is rolled back before release. */
            if (is_sp && sp_name[0]) {
                char rb_sql[128];
                snprintf(rb_sql, sizeof(rb_sql), "ROLLBACK TO SAVEPOINT %s;", sp_name);
                (void)csilk_db_exec(pool, rb_sql);
                snprintf(rb_sql, sizeof(rb_sql), "RELEASE SAVEPOINT %s;", sp_name);
                (void)csilk_db_exec(pool, rb_sql);
            } else {
                (void)csilk_db_exec(pool, "ROLLBACK;");
            }
        }
        tx_release_pinned_conn_locked();
        pthread_mutex_unlock(&g_tx_mutex);
    }
    return rc;
}

int
db_tx_scope_rollback(csilk_db_pool_t* pool, db_tx_scope_t* scope)
{
    bool was_active = (pool != NULL && scope != NULL && scope->active);
    int  rc = csilk_db_tx_scope_rollback(pool, scope);
    if (was_active) {
        tx_release_pinned_conn_locked();
        pthread_mutex_unlock(&g_tx_mutex);
    }
    return rc;
}
