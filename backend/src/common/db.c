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
    g_pool = csilk_db_pool_new(driver, dsn);
    if (!g_pool) {
        CSILK_LOG_E("Failed to create database pool driver=%s dsn=%s", driver, dsn);
        return -1;
    }

    if (!g_is_postgres) {
        csilk_db_exec(g_pool, "PRAGMA journal_mode=WAL;");
        csilk_db_exec(g_pool, "PRAGMA busy_timeout=5000;");
        csilk_db_exec(g_pool, "PRAGMA synchronous=NORMAL;");
        csilk_db_exec(g_pool, "UPDATE ai_traces SET top_p = 0.0 WHERE typeof(top_p) = 'text';");
        csilk_db_exec(g_pool,
                      "UPDATE ai_traces SET temperature = 0.0 WHERE typeof(temperature) = 'text';");
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
