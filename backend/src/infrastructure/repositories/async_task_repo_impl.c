#include "infrastructure/repositories/async_task_repo_impl.h"
#include "domain/task/entity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
format_now_str(char* out, size_t cap)
{
    time_t    now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
}

static csilk_json_t*
format_task_row(const csilk_json_t* row)
{
    if (!row || !csilk_json_is_object(row)) {
        return NULL;
    }

    csilk_json_t* obj = csilk_json_object();
    if (!obj) {
        return NULL;
    }

    csilk_json_add_number(obj, "id", (double)db_get_int(row, "id"));
    csilk_json_add_number(obj, "user_id", (double)db_get_int(row, "user_id"));
    csilk_json_add_number(obj, "ledger_id", (double)db_get_int(row, "ledger_id"));

    const char* task_type = csilk_json_get_string(row, "task_type");
    const char* status = csilk_json_get_string(row, "status");
    const char* message = csilk_json_get_string(row, "message");
    const char* error_msg = csilk_json_get_string(row, "error_message");
    const char* created_at = csilk_json_get_string(row, "created_at");
    const char* started_at = csilk_json_get_string(row, "started_at");
    const char* completed_at = csilk_json_get_string(row, "completed_at");
    const char* updated_at = csilk_json_get_string(row, "updated_at");
    const char* res_raw = csilk_json_get_string(row, "result_json");

    csilk_json_add_string(obj, "task_type", task_type ? task_type : "");
    csilk_json_add_string(obj, "status", status ? status : "pending");
    csilk_json_add_number(obj, "progress", (double)db_get_int(row, "progress"));
    csilk_json_add_string(obj, "message", message ? message : "");
    csilk_json_add_string(obj, "error_message", error_msg ? error_msg : "");
    csilk_json_add_string(obj, "created_at", created_at ? created_at : "");
    csilk_json_add_string(obj, "started_at", started_at ? started_at : "");
    csilk_json_add_string(obj, "completed_at", completed_at ? completed_at : "");
    csilk_json_add_string(obj, "updated_at", updated_at ? updated_at : "");

    bool attached_result = false;
    if (res_raw && res_raw[0]) {
        csilk_json_t* parsed = csilk_json_parse(res_raw);
        if (parsed) {
            if (csilk_json_is_object(parsed)) {
                csilk_json_add_object(obj, "result", parsed);
                attached_result = true;
            } else if (csilk_json_is_array(parsed)) {
                csilk_json_add_array(obj, "result", parsed);
                attached_result = true;
            } else {
                csilk_json_free(parsed);
            }
        }
    }
    if (!attached_result) {
        csilk_json_add_null(obj, "result");
    }

    return obj;
}

int
mf_async_task_repo_fail_stale(csilk_db_pool_t* pool)
{
    if (!pool) {
        return -1;
    }
    char now_str[32];
    format_now_str(now_str, sizeof(now_str));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_async_fail_stale", &scope) != 0) {
        return -1;
    }
    const char* params[] = {"服务重启导致后台任务中断", now_str, NULL};
    int         rc = csilk_db_exec_param(
        pool,
        "UPDATE async_tasks SET status='failed', error_message=?, completed_at=?, "
        "updated_at=CURRENT_TIMESTAMP WHERE status IN ('pending', 'running')",
        params);
    if (rc != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    return db_tx_scope_commit(pool, &scope);
}

int
mf_async_task_repo_find_active_dedupe(csilk_db_pool_t* pool,
                                      int64_t          user_id,
                                      const char*      dedupe_key,
                                      int64_t*         out_task_id)
{
    if (out_task_id) {
        *out_task_id = 0;
    }
    if (!pool || user_id <= 0 || !dedupe_key || !dedupe_key[0] || !out_task_id) {
        return 0;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char*   params[] = {uid_str, dedupe_key, NULL};
    csilk_json_t* rows =
        csilk_db_query_param_json(pool,
                                  "SELECT id FROM async_tasks WHERE user_id=? AND dedupe_key=? "
                                  "AND status IN ('pending', 'running') ORDER BY id DESC LIMIT 1",
                                  params);
    if (!rows) {
        return -1;
    }
    int found = 0;
    if (csilk_json_array_size(rows) > 0) {
        *out_task_id = db_get_int(csilk_json_array_get(rows, 0), "id");
        found = (*out_task_id > 0) ? 1 : 0;
    }
    csilk_json_free(rows);
    return found;
}

int64_t
mf_async_task_repo_insert(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          int64_t          ledger_id,
                          const char*      task_type,
                          const char*      dedupe_key,
                          const char*      initial_message)
{
    if (!pool || user_id <= 0 || !task_type || !task_type[0]) {
        return -1;
    }

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)(ledger_id > 0 ? ledger_id : 0));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_async_task_ins", &scope) != 0) {
        return -1;
    }

    const char*   params[] = {uid_str,
                              lid_str,
                              task_type,
                              initial_message ? initial_message : "",
                              dedupe_key ? dedupe_key : "",
                              NULL};
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO async_tasks (user_id, ledger_id, task_type, status, progress, message, "
        "dedupe_key) VALUES (?, ?, ?, 'pending', 0, ?, ?) RETURNING id",
        params);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }

    int64_t id = db_get_int(csilk_json_array_get(res, 0), "id");
    csilk_json_free(res);
    if (db_tx_scope_commit(pool, &scope) != 0 || id <= 0) {
        return -1;
    }
    return id;
}

int
mf_async_task_repo_mark_running(csilk_db_pool_t* pool, int64_t task_id, const char* message)
{
    if (!pool || task_id <= 0) {
        return -1;
    }
    char id_str[32], now_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)task_id);
    format_now_str(now_str, sizeof(now_str));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_async_task_run", &scope) != 0) {
        return -1;
    }
    const char* params[] = {message ? message : "", now_str, id_str, NULL};
    int         rc = csilk_db_exec_param(
        pool,
        "UPDATE async_tasks SET status='running', progress=5, message=?, started_at=?, "
        "updated_at=CURRENT_TIMESTAMP WHERE id=?",
        params);
    if (rc != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    return db_tx_scope_commit(pool, &scope);
}

int
mf_async_task_repo_update_progress(csilk_db_pool_t* pool,
                                   int64_t          task_id,
                                   int              progress,
                                   const char*      message)
{
    if (!pool || task_id <= 0) {
        return -1;
    }
    int  clamped = mf_task_clamp_progress(progress);
    char id_str[32], prog_str[16];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)task_id);
    snprintf(prog_str, sizeof(prog_str), "%d", clamped);

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_async_task_prog", &scope) != 0) {
        return -1;
    }
    const char* params[] = {prog_str, message ? message : "", id_str, NULL};
    int         rc = csilk_db_exec_param(
        pool,
        "UPDATE async_tasks SET progress=?, message=?, updated_at=CURRENT_TIMESTAMP WHERE id=?",
        params);
    if (rc != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    return db_tx_scope_commit(pool, &scope);
}

int
mf_async_task_repo_mark_completed(csilk_db_pool_t* pool,
                                  int64_t          task_id,
                                  const char*      result_json,
                                  const char*      message)
{
    if (!pool || task_id <= 0) {
        return -1;
    }
    char id_str[32], now_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)task_id);
    format_now_str(now_str, sizeof(now_str));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_async_task_done", &scope) != 0) {
        return -1;
    }
    const char* params[] = {
        result_json ? result_json : "{}", message ? message : "completed", now_str, id_str, NULL};
    int rc = csilk_db_exec_param(
        pool,
        "UPDATE async_tasks SET status='completed', progress=100, result_json=?, message=?, "
        "completed_at=?, updated_at=CURRENT_TIMESTAMP WHERE id=?",
        params);
    if (rc != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    return db_tx_scope_commit(pool, &scope);
}

int
mf_async_task_repo_mark_failed(csilk_db_pool_t* pool, int64_t task_id, const char* error_msg)
{
    if (!pool || task_id <= 0) {
        return -1;
    }
    char id_str[32], now_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)task_id);
    format_now_str(now_str, sizeof(now_str));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_async_task_fail", &scope) != 0) {
        return -1;
    }
    const char* err = (error_msg && error_msg[0]) ? error_msg : "任务执行失败";
    const char* params[] = {err, err, now_str, id_str, NULL};
    int         rc = csilk_db_exec_param(
        pool,
        "UPDATE async_tasks SET status='failed', error_message=?, message=?, completed_at=?, "
        "updated_at=CURRENT_TIMESTAMP WHERE id=?",
        params);
    if (rc != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    return db_tx_scope_commit(pool, &scope);
}

csilk_json_t*
mf_async_task_repo_get(csilk_db_pool_t* pool, int64_t user_id, int64_t task_id)
{
    if (!pool || user_id <= 0 || task_id <= 0) {
        return NULL;
    }
    char uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)task_id);

    const char*   params[] = {id_str, uid_str, NULL};
    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT id, user_id, ledger_id, task_type, status, progress, message, result_json, "
        "error_message, created_at, started_at, completed_at, updated_at "
        "FROM async_tasks WHERE id=? AND user_id=?",
        params);
    if (!rows || csilk_json_array_size(rows) == 0) {
        if (rows) {
            csilk_json_free(rows);
        }
        return NULL;
    }
    csilk_json_t* formatted = format_task_row(csilk_json_array_get(rows, 0));
    csilk_json_free(rows);
    return formatted;
}

csilk_json_t*
mf_async_task_repo_list_paged(csilk_db_pool_t* pool,
                              int64_t          user_id,
                              const char*      status_filter,
                              const char*      type_filter,
                              int64_t          page,
                              int64_t          page_size,
                              int64_t*         out_total)
{
    if (!pool || user_id <= 0) {
        if (out_total) {
            *out_total = 0;
        }
        return NULL;
    }
    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 20;
    } else if (page_size > 500) {
        page_size = 500;
    }
    int64_t row_offset = (page - 1) * page_size;

    char uid_str[32], lim_str[32], off_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lim_str, sizeof(lim_str), "%lld", (long long)page_size);
    snprintf(off_str, sizeof(off_str), "%lld", (long long)row_offset);

    bool has_status = (status_filter && status_filter[0] != '\0');
    bool has_type = (type_filter && type_filter[0] != '\0');

    const char* c_params[4] = {uid_str, NULL, NULL, NULL};
    int         cpidx = 1;
    char        count_sql[256];
    int         coff = snprintf(count_sql, sizeof(count_sql), "SELECT COUNT(*) AS cnt FROM async_tasks WHERE user_id=?");
    if (has_status && coff > 0 && (size_t)coff < sizeof(count_sql)) {
        coff += snprintf(count_sql + coff, sizeof(count_sql) - (size_t)coff, " AND status=?");
        c_params[cpidx++] = status_filter;
    }
    if (has_type && coff > 0 && (size_t)coff < sizeof(count_sql)) {
        coff += snprintf(count_sql + coff, sizeof(count_sql) - (size_t)coff, " AND task_type=?");
        c_params[cpidx++] = type_filter;
    }
    c_params[cpidx] = NULL;

    if (out_total) {
        csilk_json_t* c_rows = csilk_db_query_param_json(pool, count_sql, c_params);
        *out_total = (c_rows && csilk_json_array_size(c_rows) > 0)
                         ? db_get_int(csilk_json_array_get(c_rows, 0), "cnt")
                         : 0;
        if (c_rows) {
            csilk_json_free(c_rows);
        }
    }

    char        sql[512];
    const char* params[6] = {uid_str, NULL, NULL, NULL, NULL, NULL};
    int         pidx = 1;

    int offset = snprintf(
        sql,
        sizeof(sql),
        "SELECT id, user_id, ledger_id, task_type, status, progress, message, result_json, "
        "error_message, created_at, started_at, completed_at, updated_at "
        "FROM async_tasks WHERE user_id=?");

    if (has_status && offset > 0 && (size_t)offset < sizeof(sql)) {
        offset += snprintf(sql + offset, sizeof(sql) - (size_t)offset, " AND status=?");
        params[pidx++] = status_filter;
    }
    if (has_type && offset > 0 && (size_t)offset < sizeof(sql)) {
        offset += snprintf(sql + offset, sizeof(sql) - (size_t)offset, " AND task_type=?");
        params[pidx++] = type_filter;
    }
    snprintf(sql + offset, sizeof(sql) - (size_t)offset, " ORDER BY id DESC LIMIT ? OFFSET ?");
    params[pidx++] = lim_str;
    params[pidx++] = off_str;
    params[pidx] = NULL;

    csilk_json_t* rows = csilk_db_query_param_json(pool, sql, params);
    csilk_json_t* out = csilk_json_array();
    if (!rows || !out) {
        if (rows) {
            csilk_json_free(rows);
        }
        return out;
    }

    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* item = format_task_row(csilk_json_array_get(rows, i));
        if (item) {
            csilk_json_array_append(out, item);
        }
    }
    csilk_json_free(rows);
    return out;
}

csilk_json_t*
mf_async_task_repo_list(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        const char*      status_filter,
                        const char*      type_filter,
                        int              limit)
{
    if (limit <= 0) {
        limit = 20;
    } else if (limit > 100) {
        limit = 100;
    }
    return mf_async_task_repo_list_paged(pool, user_id, status_filter, type_filter, 1, limit, NULL);
}
