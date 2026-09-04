#pragma once

#include "infrastructure/database/database.h"
#include "infrastructure/database/statement.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mf_db_adapter_ops_s {
    const char* (*name)(void);
    int (*open)(const mf_db_config_t* config, void** out_handle);
    void (*close)(void* handle);
    int (*execute)(void* handle, const char* sql);
    int (*tx_begin)(void* handle);
    int (*tx_commit)(void* handle);
    int (*tx_rollback)(void* handle);
    int (*tx_savepoint)(void* handle, const char* name);
    int (*tx_rollback_to_savepoint)(void* handle, const char* name);
    int (*tx_release_savepoint)(void* handle, const char* name);
    int (*stmt_prepare)(void* handle, const char* sql, void** out_stmt);
    void (*stmt_close)(void* stmt);
    void (*stmt_reset)(void* stmt);
    int (*stmt_bind_int64)(void* stmt, int index, int64_t val);
    int (*stmt_bind_double)(void* stmt, int index, double val);
    int (*stmt_bind_text)(void* stmt, int index, const char* text);
    int (*stmt_bind_bool)(void* stmt, int index, bool val);
    int (*stmt_bind_null)(void* stmt, int index);
    int (*stmt_execute)(void* stmt, int64_t* out_affected_rows);
    int (*stmt_query)(void* stmt, mf_result_t** out_result);
} mf_db_adapter_ops_t;

/* 内部结构暴露给 adapter 与 statement */
struct mf_result_s {
    int     row_count;
    int     col_count;
    char**  col_names;
    char*** rows;        /* rows[r][c], NULL 表示 SQL NULL */
    int     current_row; /* 游标位置，初始 -1 */
};

#ifdef __cplusplus
}
#endif
