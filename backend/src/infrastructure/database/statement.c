#include "infrastructure/database/statement.h"
#include "csilk/csilk.h"
#include "infrastructure/database/adapter_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern void*                      mf_db_get_native_handle(mf_db_t* db);
extern const mf_db_adapter_ops_t* mf_db_get_ops(mf_db_t* db);

struct mf_stmt_s {
    mf_db_t*                   db;
    void*                      native_stmt;
    const mf_db_adapter_ops_t* ops;
};

int
mf_stmt_prepare(mf_db_t* db, const char* sql, mf_stmt_t** out_stmt)
{
    if (!db || !sql || !out_stmt) {
        return -1;
    }
    const mf_db_adapter_ops_t* ops = mf_db_get_ops(db);
    void*                      h = mf_db_get_native_handle(db);
    if (!ops || !h) {
        return -1;
    }

    void* native_stmt = NULL;
    if (ops->stmt_prepare(h, sql, &native_stmt) != 0 || !native_stmt) {
        return -1;
    }

    mf_stmt_t* stmt = calloc(1, sizeof(*stmt));
    if (!stmt) {
        ops->stmt_close(native_stmt);
        return -1;
    }
    stmt->db = db;
    stmt->native_stmt = native_stmt;
    stmt->ops = ops;

    *out_stmt = stmt;
    return 0;
}

void
mf_stmt_close(mf_stmt_t* stmt)
{
    if (!stmt) {
        return;
    }
    if (stmt->ops && stmt->native_stmt) {
        stmt->ops->stmt_close(stmt->native_stmt);
    }
    free(stmt);
}

void
mf_stmt_reset(mf_stmt_t* stmt)
{
    if (!stmt || !stmt->ops || !stmt->native_stmt) {
        return;
    }
    stmt->ops->stmt_reset(stmt->native_stmt);
}

int
mf_stmt_bind_int64(mf_stmt_t* stmt, int index, int64_t val)
{
    if (!stmt || !stmt->ops || !stmt->native_stmt) {
        return -1;
    }
    return stmt->ops->stmt_bind_int64(stmt->native_stmt, index, val);
}

int
mf_stmt_bind_double(mf_stmt_t* stmt, int index, double val)
{
    if (!stmt || !stmt->ops || !stmt->native_stmt) {
        return -1;
    }
    return stmt->ops->stmt_bind_double(stmt->native_stmt, index, val);
}

int
mf_stmt_bind_text(mf_stmt_t* stmt, int index, const char* text)
{
    if (!stmt || !stmt->ops || !stmt->native_stmt) {
        return -1;
    }
    return stmt->ops->stmt_bind_text(stmt->native_stmt, index, text);
}

int
mf_stmt_bind_bool(mf_stmt_t* stmt, int index, bool val)
{
    if (!stmt || !stmt->ops || !stmt->native_stmt) {
        return -1;
    }
    return stmt->ops->stmt_bind_bool(stmt->native_stmt, index, val);
}

int
mf_stmt_bind_null(mf_stmt_t* stmt, int index)
{
    if (!stmt || !stmt->ops || !stmt->native_stmt) {
        return -1;
    }
    return stmt->ops->stmt_bind_null(stmt->native_stmt, index);
}

int
mf_stmt_execute(mf_stmt_t* stmt, int64_t* out_affected_rows)
{
    if (!stmt || !stmt->ops || !stmt->native_stmt) {
        return -1;
    }
    return stmt->ops->stmt_execute(stmt->native_stmt, out_affected_rows);
}

int
mf_stmt_query(mf_stmt_t* stmt, mf_result_t** out_result)
{
    if (!stmt || !stmt->ops || !stmt->native_stmt || !out_result) {
        return -1;
    }
    return stmt->ops->stmt_query(stmt->native_stmt, out_result);
}

/* ====================================================================
 * 结果集游标实现
 * ==================================================================== */

bool
mf_result_next(mf_result_t* res)
{
    if (!res) {
        return false;
    }
    res->current_row++;
    return res->current_row < res->row_count;
}

int
mf_result_column_count(mf_result_t* res)
{
    return res ? res->col_count : 0;
}

const char*
mf_result_column_name(mf_result_t* res, int col_index)
{
    if (!res || col_index < 0 || col_index >= res->col_count) {
        return "";
    }
    return res->col_names[col_index] ? res->col_names[col_index] : "";
}

static int
mf_result_find_col(mf_result_t* res, const char* col_name)
{
    if (!res || !col_name) {
        return -1;
    }
    for (int i = 0; i < res->col_count; i++) {
        if (res->col_names[i] && strcasecmp(res->col_names[i], col_name) == 0) {
            return i;
        }
    }
    return -1;
}

int64_t
mf_result_get_int64(mf_result_t* res, const char* col_name)
{
    const char* txt = mf_result_get_text(res, col_name);
    if (!txt || !txt[0]) {
        return 0;
    }
    return (int64_t)strtoll(txt, NULL, 10);
}

double
mf_result_get_double(mf_result_t* res, const char* col_name)
{
    const char* txt = mf_result_get_text(res, col_name);
    if (!txt || !txt[0]) {
        return 0.0;
    }
    return strtod(txt, NULL);
}

const char*
mf_result_get_text(mf_result_t* res, const char* col_name)
{
    if (!res || res->current_row < 0 || res->current_row >= res->row_count) {
        return "";
    }
    int col = mf_result_find_col(res, col_name);
    if (col < 0) {
        return "";
    }
    char* val = res->rows[res->current_row][col];
    return val ? val : "";
}

bool
mf_result_get_bool(mf_result_t* res, const char* col_name)
{
    const char* txt = mf_result_get_text(res, col_name);
    if (!txt || !txt[0]) {
        return false;
    }
    return (strcasecmp(txt, "1") == 0 || strcasecmp(txt, "true") == 0 || strcasecmp(txt, "t") == 0);
}

bool
mf_result_is_null(mf_result_t* res, const char* col_name)
{
    if (!res || res->current_row < 0 || res->current_row >= res->row_count) {
        return true;
    }
    int col = mf_result_find_col(res, col_name);
    if (col < 0) {
        return true;
    }
    return res->rows[res->current_row][col] == NULL;
}

void
mf_result_free(mf_result_t* res)
{
    if (!res) {
        return;
    }
    if (res->col_names) {
        for (int i = 0; i < res->col_count; i++) {
            if (res->col_names[i]) {
                free(res->col_names[i]);
            }
        }
        free(res->col_names);
    }
    if (res->rows) {
        for (int r = 0; r < res->row_count; r++) {
            if (res->rows[r]) {
                for (int c = 0; c < res->col_count; c++) {
                    if (res->rows[r][c]) {
                        free(res->rows[r][c]);
                    }
                }
                free(res->rows[r]);
            }
        }
        free(res->rows);
    }
    free(res);
}

csilk_json_t*
mf_result_to_json(mf_result_t* res)
{
    csilk_json_t* arr = csilk_json_array();
    if (!res || !arr) {
        return arr;
    }

    for (int r = 0; r < res->row_count; r++) {
        csilk_json_t* row_obj = csilk_json_object();
        for (int c = 0; c < res->col_count; c++) {
            const char* name = res->col_names[c];
            char*       val = res->rows[r][c];
            if (val) {
                csilk_json_add_object(row_obj, name, csilk_json_string_new(val));
            } else {
                csilk_json_add_object(row_obj, name, csilk_json_null());
            }
        }
        csilk_json_array_append(arr, row_obj);
    }
    return arr;
}
