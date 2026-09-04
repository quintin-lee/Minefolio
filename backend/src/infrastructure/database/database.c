#include "infrastructure/database/database.h"
#include "infrastructure/database/adapter_ops.h"
#include "infrastructure/database/postgres/postgres_adapter.h"
#include "infrastructure/database/sqlite/sqlite_adapter.h"
#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct mf_db_s {
    mf_db_config_t             config;
    const mf_db_adapter_ops_t* ops;
    void*                      native_handle;
    void*                      underlying_csilk_pool;
    pthread_mutex_t            lock;
};

int
mf_db_open(const mf_db_config_t* config, mf_db_t** out_db)
{
    if (!config || !out_db) {
        return -1;
    }

    mf_db_t* db = calloc(1, sizeof(*db));
    if (!db) {
        return -1;
    }
    db->config = *config;
    pthread_mutex_init(&db->lock, NULL);

    if (config->engine == MF_DB_ENGINE_POSTGRES) {
        db->ops = mf_postgres_adapter_get_ops();
    } else {
        db->ops = mf_sqlite_adapter_get_ops();
    }

    if (db->ops->open(config, &db->native_handle) != 0) {
        pthread_mutex_destroy(&db->lock);
        free(db);
        return -1;
    }

    *out_db = db;
    return 0;
}

int
mf_db_wrap_csilk(void* csilk_pool, mf_db_engine_t engine, mf_db_t** out_db)
{
    if (!csilk_pool || !out_db) {
        return -1;
    }

    mf_db_config_t cfg = {
        .engine = engine,
        .dsn = "",
        .max_retries = 3,
        .retry_interval_ms = 50,
        .busy_timeout_ms = 5000,
    };

    mf_db_t* db = calloc(1, sizeof(*db));
    if (!db) {
        return -1;
    }
    db->config = cfg;
    db->underlying_csilk_pool = csilk_pool;
    pthread_mutex_init(&db->lock, NULL);

    if (engine == MF_DB_ENGINE_POSTGRES) {
        db->ops = mf_postgres_adapter_get_ops();
        db->ops->open(&cfg, &db->native_handle);
    } else {
        db->ops = mf_sqlite_adapter_get_ops();
        db->ops->open(&cfg, &db->native_handle);
    }

    *out_db = db;
    return 0;
}

void
mf_db_close(mf_db_t* db)
{
    if (!db) {
        return;
    }
    pthread_mutex_lock(&db->lock);
    if (db->ops && db->native_handle) {
        db->ops->close(db->native_handle);
        db->native_handle = NULL;
    }
    pthread_mutex_unlock(&db->lock);
    pthread_mutex_destroy(&db->lock);
    free(db);
}

mf_db_engine_t
mf_db_get_engine(const mf_db_t* db)
{
    return db ? db->config.engine : MF_DB_ENGINE_SQLITE;
}

int
mf_db_execute(mf_db_t* db, const char* sql)
{
    if (!db || !sql) {
        return -1;
    }
    pthread_mutex_lock(&db->lock);
    int rc = db->ops->execute(db->native_handle, sql);
    pthread_mutex_unlock(&db->lock);
    return rc;
}

int
mf_db_execute_with_retry(mf_db_t* db, const char* sql)
{
    if (!db || !sql) {
        return -1;
    }
    int max_retries = db->config.max_retries > 0 ? db->config.max_retries : 3;
    int interval_ms = db->config.retry_interval_ms > 0 ? db->config.retry_interval_ms : 50;

    int rc = -1;
    for (int attempt = 0; attempt <= max_retries; attempt++) {
        rc = mf_db_execute(db, sql);
        if (rc == 0) {
            return 0;
        }
        if (attempt < max_retries) {
            usleep((useconds_t)interval_ms * 1000);
            interval_ms *= 2; /* 指数退避 */
        }
    }
    return rc;
}

void*
mf_db_get_underlying_pool(mf_db_t* db)
{
    return db ? db->underlying_csilk_pool : NULL;
}

/* 内部辅助：获取原生句柄与操作表供事务/语句使用 */
void*
mf_db_get_native_handle(mf_db_t* db)
{
    return db ? db->native_handle : NULL;
}

const mf_db_adapter_ops_t*
mf_db_get_ops(mf_db_t* db)
{
    return db ? db->ops : NULL;
}
