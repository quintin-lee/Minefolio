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

    char        drv_buf[64] = {0};
    char        dsn_buf[512] = {0};
    const char* driver_env = config_secret_get("DB_DRIVER", drv_buf, sizeof(drv_buf));
    const char* dsn_env = config_secret_get("DB_DSN", dsn_buf, sizeof(dsn_buf));

    /* Read persisted config (written by /system/setup) */
    char cfg_driver[32] = {0};
    char cfg_dsn[512] = {0};
    config_get_str("config/db.json", "driver", cfg_driver, sizeof(cfg_driver));
    config_get_str("config/db.json", "dsn", cfg_dsn, sizeof(cfg_dsn));

    const char* driver = driver_env ? driver_env : cfg_driver[0] ? cfg_driver : "sqlite";
    const char* dsn = dsn_env      ? dsn_env
                      : cfg_dsn[0] ? cfg_dsn
                                   : (strcmp(driver, "postgres") == 0
                                          ? "host=localhost user=minefolio dbname=minefolio"
                                          : "./data/minefolio.db");

    g_is_postgres = (strcmp(driver, "postgres") == 0);
    CSILK_LOG_I("DB driver=%s dsn=%s", driver, dsn);

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
        return -1;
    }

    /* Auto-baseline versions <= 7 if existing legacy production database (users table exists) */
    csilk_migration_engine_set_baseline(engine, 7, "users");

    int applied_count = 0;
    int rc = csilk_migration_apply(engine, &applied_count);
    if (rc != 0) {
        CSILK_LOG_E("Database migration failed");
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
db_in_transaction(csilk_db_pool_t* pool)
{
    return csilk_db_in_transaction(pool);
}

int
db_tx_scope_begin(csilk_db_pool_t* pool, const char* name, db_tx_scope_t* scope)
{
    return csilk_db_tx_scope_begin(pool, name, scope);
}

int
db_tx_scope_commit(csilk_db_pool_t* pool, db_tx_scope_t* scope)
{
    return csilk_db_tx_scope_commit(pool, scope);
}

int
db_tx_scope_rollback(csilk_db_pool_t* pool, db_tx_scope_t* scope)
{
    return csilk_db_tx_scope_rollback(pool, scope);
}
