#include "infrastructure/database/sqlite/sqlite_adapter.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    sqlite3* db;
    bool     owns_db;
} mf_sqlite_handle_t;

typedef struct {
    sqlite3_stmt* stmt;
    sqlite3*      db;
} mf_sqlite_stmt_t;

static const char*
sqlite_adapter_name(void)
{
    return "sqlite";
}

static int
sqlite_adapter_open(const mf_db_config_t* config, void** out_handle)
{
    if (!config || !out_handle) {
        return -1;
    }

    const char* dsn = config->dsn && config->dsn[0] ? config->dsn : ":memory:";
    sqlite3*    db = NULL;
    int         rc = sqlite3_open_v2(
        dsn, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
    if (rc != SQLITE_OK) {
        if (db) {
            sqlite3_close_v2(db);
        }
        return -1;
    }

    /* 设定基础性能与重试配置 */
    int busy_timeout = config->busy_timeout_ms > 0 ? config->busy_timeout_ms : 5000;
    sqlite3_busy_timeout(db, busy_timeout);

    if (strcmp(dsn, ":memory:") != 0) {
        sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    }
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);

    mf_sqlite_handle_t* h = calloc(1, sizeof(*h));
    if (!h) {
        sqlite3_close_v2(db);
        return -1;
    }
    h->db = db;
    h->owns_db = true;

    *out_handle = h;
    return 0;
}

int
mf_sqlite_adapter_wrap_native(sqlite3* db, void** out_handle)
{
    if (!db || !out_handle) {
        return -1;
    }
    mf_sqlite_handle_t* h = calloc(1, sizeof(*h));
    if (!h) {
        return -1;
    }
    h->db = db;
    h->owns_db = false;
    *out_handle = h;
    return 0;
}

static void
sqlite_adapter_close(void* handle)
{
    if (!handle) {
        return;
    }
    mf_sqlite_handle_t* h = (mf_sqlite_handle_t*)handle;
    if (h->db && h->owns_db) {
        sqlite3_close_v2(h->db);
    }
    free(h);
}

static int
sqlite_adapter_execute(void* handle, const char* sql)
{
    if (!handle || !sql) {
        return -1;
    }
    mf_sqlite_handle_t* h = (mf_sqlite_handle_t*)handle;
    char*               errmsg = NULL;
    int                 rc = sqlite3_exec(h->db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        if (errmsg) {
            sqlite3_free(errmsg);
        }
        return -1;
    }
    return 0;
}

static int
sqlite_adapter_tx_begin(void* handle)
{
    return sqlite_adapter_execute(handle, "BEGIN TRANSACTION;");
}

static int
sqlite_adapter_tx_commit(void* handle)
{
    return sqlite_adapter_execute(handle, "COMMIT;");
}

static int
sqlite_adapter_tx_rollback(void* handle)
{
    return sqlite_adapter_execute(handle, "ROLLBACK;");
}

static int
sqlite_adapter_tx_savepoint(void* handle, const char* name)
{
    if (!handle || !name || !name[0]) {
        return -1;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "SAVEPOINT %s;", name);
    return sqlite_adapter_execute(handle, buf);
}

static int
sqlite_adapter_tx_rollback_to_savepoint(void* handle, const char* name)
{
    if (!handle || !name || !name[0]) {
        return -1;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "ROLLBACK TO %s;", name);
    return sqlite_adapter_execute(handle, buf);
}

static int
sqlite_adapter_tx_release_savepoint(void* handle, const char* name)
{
    if (!handle || !name || !name[0]) {
        return -1;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "RELEASE %s;", name);
    return sqlite_adapter_execute(handle, buf);
}

static int
sqlite_adapter_stmt_prepare(void* handle, const char* sql, void** out_stmt)
{
    if (!handle || !sql || !out_stmt) {
        return -1;
    }
    mf_sqlite_handle_t* h = (mf_sqlite_handle_t*)handle;
    sqlite3_stmt*       stmt = NULL;
    int                 rc = sqlite3_prepare_v2(h->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) {
        return -1;
    }

    mf_sqlite_stmt_t* s = calloc(1, sizeof(*s));
    if (!s) {
        sqlite3_finalize(stmt);
        return -1;
    }
    s->stmt = stmt;
    s->db = h->db;

    *out_stmt = s;
    return 0;
}

static void
sqlite_adapter_stmt_close(void* stmt)
{
    if (!stmt) {
        return;
    }
    mf_sqlite_stmt_t* s = (mf_sqlite_stmt_t*)stmt;
    if (s->stmt) {
        sqlite3_finalize(s->stmt);
    }
    free(s);
}

static void
sqlite_adapter_stmt_reset(void* stmt)
{
    if (!stmt) {
        return;
    }
    mf_sqlite_stmt_t* s = (mf_sqlite_stmt_t*)stmt;
    if (s->stmt) {
        sqlite3_reset(s->stmt);
        sqlite3_clear_bindings(s->stmt);
    }
}

static int
sqlite_adapter_stmt_bind_int64(void* stmt, int index, int64_t val)
{
    if (!stmt || index < 1) {
        return -1;
    }
    mf_sqlite_stmt_t* s = (mf_sqlite_stmt_t*)stmt;
    return sqlite3_bind_int64(s->stmt, index, val) == SQLITE_OK ? 0 : -1;
}

static int
sqlite_adapter_stmt_bind_double(void* stmt, int index, double val)
{
    if (!stmt || index < 1) {
        return -1;
    }
    mf_sqlite_stmt_t* s = (mf_sqlite_stmt_t*)stmt;
    return sqlite3_bind_double(s->stmt, index, val) == SQLITE_OK ? 0 : -1;
}

static int
sqlite_adapter_stmt_bind_text(void* stmt, int index, const char* text)
{
    if (!stmt || index < 1) {
        return -1;
    }
    mf_sqlite_stmt_t* s = (mf_sqlite_stmt_t*)stmt;
    const char*       val = text ? text : "";
    return sqlite3_bind_text(s->stmt, index, val, -1, SQLITE_TRANSIENT) == SQLITE_OK ? 0 : -1;
}

static int
sqlite_adapter_stmt_bind_bool(void* stmt, int index, bool val)
{
    if (!stmt || index < 1) {
        return -1;
    }
    mf_sqlite_stmt_t* s = (mf_sqlite_stmt_t*)stmt;
    return sqlite3_bind_int(s->stmt, index, val ? 1 : 0) == SQLITE_OK ? 0 : -1;
}

static int
sqlite_adapter_stmt_bind_null(void* stmt, int index)
{
    if (!stmt || index < 1) {
        return -1;
    }
    mf_sqlite_stmt_t* s = (mf_sqlite_stmt_t*)stmt;
    return sqlite3_bind_null(s->stmt, index) == SQLITE_OK ? 0 : -1;
}

static int
sqlite_adapter_stmt_execute(void* stmt, int64_t* out_affected_rows)
{
    if (!stmt) {
        return -1;
    }
    mf_sqlite_stmt_t* s = (mf_sqlite_stmt_t*)stmt;
    int               rc = sqlite3_step(s->stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        return -1;
    }
    if (out_affected_rows) {
        *out_affected_rows = (int64_t)sqlite3_changes(s->db);
    }
    return 0;
}

static int
sqlite_adapter_stmt_query(void* stmt, mf_result_t** out_result)
{
    if (!stmt || !out_result) {
        return -1;
    }
    mf_sqlite_stmt_t* s = (mf_sqlite_stmt_t*)stmt;

    int cols = sqlite3_column_count(s->stmt);
    if (cols < 0) {
        return -1;
    }

    mf_result_t* res = calloc(1, sizeof(*res));
    if (!res) {
        return -1;
    }
    res->col_count = cols;
    res->current_row = -1;

    if (cols > 0) {
        res->col_names = calloc((size_t)cols, sizeof(char*));
        if (!res->col_names) {
            free(res);
            return -1;
        }
        for (int i = 0; i < cols; i++) {
            const char* name = sqlite3_column_name(s->stmt, i);
            res->col_names[i] = name ? strdup(name) : strdup("");
        }
    }

    int capacity = 8;
    res->rows = calloc((size_t)capacity, sizeof(char**));
    if (!res->rows) {
        for (int i = 0; i < cols; i++) {
            free(res->col_names[i]);
        }
        free(res->col_names);
        free(res);
        return -1;
    }

    while (sqlite3_step(s->stmt) == SQLITE_ROW) {
        if (res->row_count >= capacity) {
            capacity *= 2;
            char*** new_rows = realloc(res->rows, (size_t)capacity * sizeof(char**));
            if (!new_rows) {
                break;
            }
            res->rows = new_rows;
        }

        char** row_vals = calloc((size_t)cols, sizeof(char*));
        if (!row_vals) {
            break;
        }
        for (int i = 0; i < cols; i++) {
            if (sqlite3_column_type(s->stmt, i) == SQLITE_NULL) {
                row_vals[i] = NULL;
            } else {
                const unsigned char* txt = sqlite3_column_text(s->stmt, i);
                row_vals[i] = txt ? strdup((const char*)txt) : strdup("");
            }
        }
        res->rows[res->row_count++] = row_vals;
    }

    *out_result = res;
    return 0;
}

static const mf_db_adapter_ops_t s_sqlite_ops = {
    .name = sqlite_adapter_name,
    .open = sqlite_adapter_open,
    .close = sqlite_adapter_close,
    .execute = sqlite_adapter_execute,
    .tx_begin = sqlite_adapter_tx_begin,
    .tx_commit = sqlite_adapter_tx_commit,
    .tx_rollback = sqlite_adapter_tx_rollback,
    .tx_savepoint = sqlite_adapter_tx_savepoint,
    .tx_rollback_to_savepoint = sqlite_adapter_tx_rollback_to_savepoint,
    .tx_release_savepoint = sqlite_adapter_tx_release_savepoint,
    .stmt_prepare = sqlite_adapter_stmt_prepare,
    .stmt_close = sqlite_adapter_stmt_close,
    .stmt_reset = sqlite_adapter_stmt_reset,
    .stmt_bind_int64 = sqlite_adapter_stmt_bind_int64,
    .stmt_bind_double = sqlite_adapter_stmt_bind_double,
    .stmt_bind_text = sqlite_adapter_stmt_bind_text,
    .stmt_bind_bool = sqlite_adapter_stmt_bind_bool,
    .stmt_bind_null = sqlite_adapter_stmt_bind_null,
    .stmt_execute = sqlite_adapter_stmt_execute,
    .stmt_query = sqlite_adapter_stmt_query,
};

const mf_db_adapter_ops_t*
mf_sqlite_adapter_get_ops(void)
{
    return &s_sqlite_ops;
}
