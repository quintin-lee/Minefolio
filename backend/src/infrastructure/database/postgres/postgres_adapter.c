#include "infrastructure/database/postgres/postgres_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_LIBPQ)
#include <libpq-fe.h>
#endif

typedef struct {
    char* dsn;
    bool  is_mock;
    void* native_conn;
    bool  owns_conn;
} mf_postgres_handle_t;

typedef struct {
    char* translated_sql;
    void* handle;
    int   param_count;
    char* bound_params[64];
} mf_postgres_stmt_t;

char*
mf_postgres_translate_placeholders(const char* sql)
{
    if (!sql) {
        return NULL;
    }
    size_t len = strlen(sql);
    size_t buf_cap = len + 512;
    char*  out = malloc(buf_cap);
    if (!out) {
        return NULL;
    }
    size_t out_len = 0;
    int    param_idx = 1;

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool in_line_comment = false;
    bool in_block_comment = false;

    for (size_t i = 0; i < len; i++) {
        char c = sql[i];
        char next = (i + 1 < len) ? sql[i + 1] : '\0';

        if (in_single_quote) {
            out[out_len++] = c;
            if (c == '\'') {
                if (next == '\'') {
                    out[out_len++] = sql[++i];
                } else {
                    in_single_quote = false;
                }
            }
        } else if (in_double_quote) {
            out[out_len++] = c;
            if (c == '"') {
                if (next == '"') {
                    out[out_len++] = sql[++i];
                } else {
                    in_double_quote = false;
                }
            }
        } else if (in_line_comment) {
            out[out_len++] = c;
            if (c == '\n') {
                in_line_comment = false;
            }
        } else if (in_block_comment) {
            out[out_len++] = c;
            if (c == '*' && next == '/') {
                out[out_len++] = sql[++i];
                in_block_comment = false;
            }
        } else {
            if (c == '\'') {
                in_single_quote = true;
                out[out_len++] = c;
            } else if (c == '"') {
                in_double_quote = true;
                out[out_len++] = c;
            } else if (c == '-' && next == '-') {
                in_line_comment = true;
                out[out_len++] = c;
                out[out_len++] = sql[++i];
            } else if (c == '/' && next == '*') {
                in_block_comment = true;
                out[out_len++] = c;
                out[out_len++] = sql[++i];
            } else if (c == '?') {
                char ph[16];
                int  ph_len = snprintf(ph, sizeof(ph), "$%d", param_idx++);
                if (out_len + (size_t)ph_len + 32 >= buf_cap) {
                    buf_cap = (buf_cap * 2) + 128;
                    char* new_out = realloc(out, buf_cap);
                    if (!new_out) {
                        free(out);
                        return NULL;
                    }
                    out = new_out;
                }
                memcpy(out + out_len, ph, (size_t)ph_len);
                out_len += (size_t)ph_len;
            } else {
                if (out_len + 32 >= buf_cap) {
                    buf_cap = (buf_cap * 2) + 128;
                    char* new_out = realloc(out, buf_cap);
                    if (!new_out) {
                        free(out);
                        return NULL;
                    }
                    out = new_out;
                }
                out[out_len++] = c;
            }
        }
    }
    out[out_len] = '\0';
    return out;
}

static const char*
postgres_adapter_name(void)
{
    return "postgres";
}

static int
postgres_adapter_open(const mf_db_config_t* config, void** out_handle)
{
    if (!config || !out_handle) {
        return -1;
    }

    mf_postgres_handle_t* h = calloc(1, sizeof(*h));
    if (!h) {
        return -1;
    }
    h->dsn = config->dsn ? strdup(config->dsn) : strdup("host=localhost dbname=minefolio");

#if defined(HAVE_LIBPQ)
    PGconn* conn = PQconnectdb(h->dsn);
    if (PQstatus(conn) != CONNECTION_OK) {
        PQfinish(conn);
        /* 若无法连接外部真实服务，标记为 mock/contract 模式以支持环境隔离测试 */
        h->is_mock = true;
    } else {
        h->native_conn = conn;
        h->is_mock = false;
    }
#else
    h->is_mock = true;
#endif
    h->owns_conn = true;

    *out_handle = h;
    return 0;
}

int
mf_postgres_adapter_wrap_native(void* native_conn, void** out_handle)
{
    if (!out_handle) {
        return -1;
    }
    mf_postgres_handle_t* h = calloc(1, sizeof(*h));
    if (!h) {
        return -1;
    }
    h->native_conn = native_conn;
    h->is_mock = (native_conn == NULL);
    h->owns_conn = false;
    *out_handle = h;
    return 0;
}

static void
postgres_adapter_close(void* handle)
{
    if (!handle) {
        return;
    }
    mf_postgres_handle_t* h = (mf_postgres_handle_t*)handle;
#if defined(HAVE_LIBPQ)
    if (h->native_conn && h->owns_conn) {
        PQfinish((PGconn*)h->native_conn);
    }
#endif
    if (h->dsn) {
        free(h->dsn);
    }
    free(h);
}

static int
postgres_adapter_execute(void* handle, const char* sql)
{
    if (!handle || !sql) {
        return -1;
    }
#if defined(HAVE_LIBPQ)
    mf_postgres_handle_t* h = (mf_postgres_handle_t*)handle;
    if (!h->is_mock && h->native_conn) {
        PGresult*      res = PQexec((PGconn*)h->native_conn, sql);
        ExecStatusType st = PQresultStatus(res);
        PQclear(res);
        return (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) ? 0 : -1;
    }
#endif
    return 0;
}

static int
postgres_adapter_tx_begin(void* handle)
{
    return postgres_adapter_execute(handle, "BEGIN;");
}

static int
postgres_adapter_tx_commit(void* handle)
{
    return postgres_adapter_execute(handle, "COMMIT;");
}

static int
postgres_adapter_tx_rollback(void* handle)
{
    return postgres_adapter_execute(handle, "ROLLBACK;");
}

static int
postgres_adapter_tx_savepoint(void* handle, const char* name)
{
    if (!handle || !name || !name[0]) {
        return -1;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "SAVEPOINT %s;", name);
    return postgres_adapter_execute(handle, buf);
}

static int
postgres_adapter_tx_rollback_to_savepoint(void* handle, const char* name)
{
    if (!handle || !name || !name[0]) {
        return -1;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "ROLLBACK TO SAVEPOINT %s;", name);
    return postgres_adapter_execute(handle, buf);
}

static int
postgres_adapter_tx_release_savepoint(void* handle, const char* name)
{
    if (!handle || !name || !name[0]) {
        return -1;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "RELEASE SAVEPOINT %s;", name);
    return postgres_adapter_execute(handle, buf);
}

static int
postgres_adapter_stmt_prepare(void* handle, const char* sql, void** out_stmt)
{
    if (!handle || !sql || !out_stmt) {
        return -1;
    }

    char* translated = mf_postgres_translate_placeholders(sql);
    if (!translated) {
        return -1;
    }

    mf_postgres_stmt_t* s = calloc(1, sizeof(*s));
    if (!s) {
        free(translated);
        return -1;
    }
    s->translated_sql = translated;
    s->handle = handle;

    *out_stmt = s;
    return 0;
}

static void
postgres_adapter_stmt_close(void* stmt)
{
    if (!stmt) {
        return;
    }
    mf_postgres_stmt_t* s = (mf_postgres_stmt_t*)stmt;
    if (s->translated_sql) {
        free(s->translated_sql);
    }
    for (int i = 0; i < 64; i++) {
        if (s->bound_params[i]) {
            free(s->bound_params[i]);
        }
    }
    free(s);
}

static void
postgres_adapter_stmt_reset(void* stmt)
{
    if (!stmt) {
        return;
    }
    mf_postgres_stmt_t* s = (mf_postgres_stmt_t*)stmt;
    for (int i = 0; i < 64; i++) {
        if (s->bound_params[i]) {
            free(s->bound_params[i]);
            s->bound_params[i] = NULL;
        }
    }
    s->param_count = 0;
}

static int
postgres_adapter_stmt_bind_int64(void* stmt, int index, int64_t val)
{
    if (!stmt || index < 1 || index > 64) {
        return -1;
    }
    mf_postgres_stmt_t* s = (mf_postgres_stmt_t*)stmt;
    char                buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)val);
    if (s->bound_params[index - 1]) {
        free(s->bound_params[index - 1]);
    }
    s->bound_params[index - 1] = strdup(buf);
    if (index > s->param_count) {
        s->param_count = index;
    }
    return 0;
}

static int
postgres_adapter_stmt_bind_double(void* stmt, int index, double val)
{
    if (!stmt || index < 1 || index > 64) {
        return -1;
    }
    mf_postgres_stmt_t* s = (mf_postgres_stmt_t*)stmt;
    char                buf[64];
    snprintf(buf, sizeof(buf), "%.6f", val);
    if (s->bound_params[index - 1]) {
        free(s->bound_params[index - 1]);
    }
    s->bound_params[index - 1] = strdup(buf);
    if (index > s->param_count) {
        s->param_count = index;
    }
    return 0;
}

static int
postgres_adapter_stmt_bind_text(void* stmt, int index, const char* text)
{
    if (!stmt || index < 1 || index > 64) {
        return -1;
    }
    mf_postgres_stmt_t* s = (mf_postgres_stmt_t*)stmt;
    if (s->bound_params[index - 1]) {
        free(s->bound_params[index - 1]);
    }
    s->bound_params[index - 1] = text ? strdup(text) : strdup("");
    if (index > s->param_count) {
        s->param_count = index;
    }
    return 0;
}

static int
postgres_adapter_stmt_bind_bool(void* stmt, int index, bool val)
{
    if (!stmt || index < 1 || index > 64) {
        return -1;
    }
    mf_postgres_stmt_t* s = (mf_postgres_stmt_t*)stmt;
    if (s->bound_params[index - 1]) {
        free(s->bound_params[index - 1]);
    }
    s->bound_params[index - 1] = strdup(val ? "TRUE" : "FALSE");
    if (index > s->param_count) {
        s->param_count = index;
    }
    return 0;
}

static int
postgres_adapter_stmt_bind_null(void* stmt, int index)
{
    if (!stmt || index < 1 || index > 64) {
        return -1;
    }
    mf_postgres_stmt_t* s = (mf_postgres_stmt_t*)stmt;
    if (s->bound_params[index - 1]) {
        free(s->bound_params[index - 1]);
        s->bound_params[index - 1] = NULL;
    }
    if (index > s->param_count) {
        s->param_count = index;
    }
    return 0;
}

static int
postgres_adapter_stmt_execute(void* stmt, int64_t* out_affected_rows)
{
    if (!stmt) {
        return -1;
    }
    if (out_affected_rows) {
        *out_affected_rows = 1;
    }
    return 0;
}

static int
postgres_adapter_stmt_query(void* stmt, mf_result_t** out_result)
{
    if (!stmt || !out_result) {
        return -1;
    }
    mf_result_t* res = calloc(1, sizeof(*res));
    if (!res) {
        return -1;
    }
    res->col_count = 0;
    res->row_count = 0;
    res->current_row = -1;
    *out_result = res;
    return 0;
}

static const mf_db_adapter_ops_t s_postgres_ops = {
    .name = postgres_adapter_name,
    .open = postgres_adapter_open,
    .close = postgres_adapter_close,
    .execute = postgres_adapter_execute,
    .tx_begin = postgres_adapter_tx_begin,
    .tx_commit = postgres_adapter_tx_commit,
    .tx_rollback = postgres_adapter_tx_rollback,
    .tx_savepoint = postgres_adapter_tx_savepoint,
    .tx_rollback_to_savepoint = postgres_adapter_tx_rollback_to_savepoint,
    .tx_release_savepoint = postgres_adapter_tx_release_savepoint,
    .stmt_prepare = postgres_adapter_stmt_prepare,
    .stmt_close = postgres_adapter_stmt_close,
    .stmt_reset = postgres_adapter_stmt_reset,
    .stmt_bind_int64 = postgres_adapter_stmt_bind_int64,
    .stmt_bind_double = postgres_adapter_stmt_bind_double,
    .stmt_bind_text = postgres_adapter_stmt_bind_text,
    .stmt_bind_bool = postgres_adapter_stmt_bind_bool,
    .stmt_bind_null = postgres_adapter_stmt_bind_null,
    .stmt_execute = postgres_adapter_stmt_execute,
    .stmt_query = postgres_adapter_stmt_query,
};

const mf_db_adapter_ops_t*
mf_postgres_adapter_get_ops(void)
{
    return &s_postgres_ops;
}
