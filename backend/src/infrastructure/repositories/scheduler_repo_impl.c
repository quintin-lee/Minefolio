#include "infrastructure/repositories/scheduler_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
get_now_str(char* buf, size_t len)
{
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_now);
}

csilk_json_t*
mf_scheduler_repo_job_list(
    void* pool, int64_t user_id, bool is_admin, int64_t page, int64_t page_size, int64_t* out_total)
{
    if (!pool) {
        return NULL;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 20;
    }
    int64_t offset = (page - 1) * page_size;

    char uid_str[32], limit_str[32], offset_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(limit_str, sizeof(limit_str), "%lld", (long long)page_size);
    snprintf(offset_str, sizeof(offset_str), "%lld", (long long)offset);

    const char* count_sql =
        "SELECT COUNT(*) as total FROM scheduled_jobs WHERE (user_id = ? OR is_builtin = 1)";

    const char* list_sql = "SELECT * FROM scheduled_jobs WHERE (user_id = ? OR is_builtin = 1) "
                           "ORDER BY is_builtin DESC, id ASC LIMIT ? OFFSET ?";

    if (out_total) {
        *out_total = 0;
        csilk_json_t* cres =
            csilk_db_query_param_json(db_pool, count_sql, (const char*[]){uid_str, NULL});
        if (cres) {
            if (csilk_json_array_size(cres) > 0) {
                csilk_json_t* crow = csilk_json_array_get(cres, 0);
                *out_total = db_get_int(crow, "total");
            }
            csilk_json_free(cres);
        }
    }

    return csilk_db_query_param_json(
        db_pool, list_sql, (const char*[]){uid_str, limit_str, offset_str, NULL});
}

csilk_json_t*
mf_scheduler_repo_job_get(void* pool, int64_t user_id, bool is_admin, int64_t job_id)
{
    if (!pool || job_id <= 0) {
        return NULL;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char jid_str[32], uid_str[32];
    snprintf(jid_str, sizeof(jid_str), "%lld", (long long)job_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char* sql =
        "SELECT * FROM scheduled_jobs WHERE id = ? AND (user_id = ? OR is_builtin = 1) LIMIT 1";

    csilk_json_t* res =
        csilk_db_query_param_json(db_pool, sql, (const char*[]){jid_str, uid_str, NULL});
    if (!res) {
        return NULL;
    }

    if (csilk_json_array_size(res) == 0) {
        csilk_json_free(res);
        return NULL;
    }

    csilk_json_t* row = csilk_json_array_get(res, 0);
    csilk_json_t* item = csilk_json_copy(row);
    csilk_json_free(res);
    return item;
}

int64_t
mf_scheduler_repo_job_create(void*       pool,
                             int64_t     user_id,
                             const char* name,
                             const char* desc,
                             const char* job_type,
                             const char* trigger_type,
                             const char* trigger_cfg,
                             const char* payload_cfg,
                             const char* delivery_channel,
                             const char* delivery_cfg,
                             const char* next_run_at)
{
    if (!pool || !name || !job_type || !trigger_type) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char* sql =
        "INSERT INTO scheduled_jobs (user_id, name, description, job_type, trigger_type, "
        "trigger_config, payload_config, delivery_channel, delivery_config, next_run_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id";

    csilk_json_t* res = csilk_db_query_param_json(
        db_pool,
        sql,
        (const char*[]){uid_str,
                        name,
                        desc ? desc : "",
                        job_type,
                        trigger_type,
                        trigger_cfg ? trigger_cfg : "{}",
                        payload_cfg ? payload_cfg : "{}",
                        delivery_channel ? delivery_channel : "user_default",
                        delivery_cfg ? delivery_cfg : "{}",
                        next_run_at ? next_run_at : "",
                        NULL});
    if (!res) {
        return -1;
    }

    int64_t new_id = -1;
    if (csilk_json_array_size(res) > 0) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        new_id = db_get_int(row, "id");
    }
    csilk_json_free(res);
    return new_id;
}

int
mf_scheduler_repo_job_update(void*       pool,
                             int64_t     user_id,
                             bool        is_admin,
                             int64_t     job_id,
                             const char* name,
                             const char* desc,
                             const char* trigger_type,
                             const char* trigger_cfg,
                             const char* payload_cfg,
                             const char* delivery_channel,
                             const char* delivery_cfg,
                             const char* next_run_at)
{
    if (!pool || job_id <= 0 || !name || !trigger_type) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char jid_str[32], uid_str[32];
    snprintf(jid_str, sizeof(jid_str), "%lld", (long long)job_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char* sql = is_admin ? "UPDATE scheduled_jobs SET name = ?, description = ?, "
                                 "trigger_type = ?, trigger_config = ?, "
                                 "payload_config = ?, delivery_channel = ?, delivery_config = ?, "
                                 "next_run_at = ?, updated_at = CURRENT_TIMESTAMP "
                                 "WHERE id = ? AND (user_id = ? OR user_id = 0)"
                               : "UPDATE scheduled_jobs SET name = ?, description = ?, "
                                 "trigger_type = ?, trigger_config = ?, "
                                 "payload_config = ?, delivery_channel = ?, delivery_config = ?, "
                                 "next_run_at = ?, updated_at = CURRENT_TIMESTAMP "
                                 "WHERE id = ? AND user_id = ?";

    return csilk_db_exec_param(db_pool,
                               sql,
                               (const char*[]){name,
                                               desc ? desc : "",
                                               trigger_type,
                                               trigger_cfg ? trigger_cfg : "{}",
                                               payload_cfg ? payload_cfg : "{}",
                                               delivery_channel ? delivery_channel : "user_default",
                                               delivery_cfg ? delivery_cfg : "{}",
                                               next_run_at ? next_run_at : "",
                                               jid_str,
                                               uid_str,
                                               NULL});
}

int
mf_scheduler_repo_job_delete(void* pool, int64_t user_id, int64_t job_id)
{
    if (!pool || job_id <= 0) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char jid_str[32], uid_str[32];
    snprintf(jid_str, sizeof(jid_str), "%lld", (long long)job_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    /* Built-in tasks (user_id = 0 or is_builtin = 1) cannot be deleted */
    const char* sql = "DELETE FROM scheduled_jobs WHERE id = ? AND user_id = ? AND is_builtin = 0 "
                      "AND user_id != 0";
    return csilk_db_exec_param(db_pool, sql, (const char*[]){jid_str, uid_str, NULL});
}

int
mf_scheduler_repo_job_set_enabled(
    void* pool, int64_t user_id, bool is_admin, int64_t job_id, bool enabled)
{
    (void)is_admin;
    if (!pool || job_id <= 0) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char jid_str[32], uid_str[32], en_str[8];
    snprintf(jid_str, sizeof(jid_str), "%lld", (long long)job_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(en_str, sizeof(en_str), "%d", enabled ? 1 : 0);

    const char* sql = "UPDATE scheduled_jobs SET enabled = ?, updated_at = CURRENT_TIMESTAMP "
                      "WHERE id = ? AND (user_id = ? OR is_builtin = 1)";

    return csilk_db_exec_param(db_pool, sql, (const char*[]){en_str, jid_str, uid_str, NULL});
}

int
mf_scheduler_repo_job_update_last_run(
    void* pool, int64_t job_id, const char* status, int duration_ms, const char* next_run_at)
{
    if (!pool || job_id <= 0) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char jid_str[32], dur_str[32], now_str[64];
    snprintf(jid_str, sizeof(jid_str), "%lld", (long long)job_id);
    snprintf(dur_str, sizeof(dur_str), "%d", duration_ms);
    get_now_str(now_str, sizeof(now_str));

    if (next_run_at && *next_run_at) {
        const char* sql = "UPDATE scheduled_jobs SET last_run_at = ?, last_run_status = ?, "
                          "last_run_duration_ms = ?, "
                          "next_run_at = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?";
        return csilk_db_exec_param(
            db_pool,
            sql,
            (const char*[]){now_str, status ? status : "", dur_str, next_run_at, jid_str, NULL});
    }

    const char* sql =
        "UPDATE scheduled_jobs SET last_run_at = ?, last_run_status = ?, last_run_duration_ms = ?, "
        "updated_at = CURRENT_TIMESTAMP WHERE id = ?";

    return csilk_db_exec_param(
        db_pool, sql, (const char*[]){now_str, status ? status : "", dur_str, jid_str, NULL});
}

int64_t
mf_scheduler_repo_job_create_builtin(void*       pool,
                                     const char* name,
                                     const char* desc,
                                     const char* job_type,
                                     const char* trigger_type,
                                     const char* trigger_cfg,
                                     const char* payload_cfg,
                                     const char* delivery_channel,
                                     const char* delivery_cfg,
                                     const char* next_run_at)
{
    if (!pool || !name || !job_type || !trigger_type) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    const char* sql =
        "INSERT INTO scheduled_jobs (user_id, name, description, job_type, trigger_type, "
        "trigger_config, payload_config, delivery_channel, delivery_config, enabled, is_builtin, "
        "next_run_at) "
        "VALUES (0, ?, ?, ?, ?, ?, ?, ?, ?, 1, 1, ?) RETURNING id";

    csilk_json_t* res =
        csilk_db_query_param_json(db_pool,
                                  sql,
                                  (const char*[]){name,
                                                  desc ? desc : "",
                                                  job_type,
                                                  trigger_type,
                                                  trigger_cfg ? trigger_cfg : "{}",
                                                  payload_cfg ? payload_cfg : "{}",
                                                  delivery_channel ? delivery_channel : "silent_db",
                                                  delivery_cfg ? delivery_cfg : "{}",
                                                  next_run_at ? next_run_at : "",
                                                  NULL});
    if (!res) {
        return -1;
    }

    int64_t new_id = -1;
    if (csilk_json_array_size(res) > 0) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        new_id = db_get_int(row, "id");
    }
    csilk_json_free(res);
    return new_id;
}

csilk_json_t*
mf_scheduler_repo_job_get_by_name(void* pool, const char* name)
{
    if (!pool || !name || !*name) {
        return NULL;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    const char*   sql = "SELECT * FROM scheduled_jobs WHERE name = ? LIMIT 1";
    csilk_json_t* res = csilk_db_query_param_json(db_pool, sql, (const char*[]){name, NULL});
    if (!res) {
        return NULL;
    }

    if (csilk_json_array_size(res) == 0) {
        csilk_json_free(res);
        return NULL;
    }

    csilk_json_t* row = csilk_json_array_get(res, 0);
    csilk_json_t* item = csilk_json_copy(row);
    csilk_json_free(res);
    return item;
}

bool
mf_scheduler_repo_is_builtin_enabled(void* pool, const char* handler_name)
{
    if (!pool || !handler_name || !*handler_name) {
        return true;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char like_pattern[128];
    snprintf(like_pattern, sizeof(like_pattern), "%%\"handler\":\"%s\"%%", handler_name);

    const char* sql =
        "SELECT enabled FROM scheduled_jobs WHERE is_builtin = 1 AND payload_config LIKE ? LIMIT 1";

    csilk_json_t* res =
        csilk_db_query_param_json(db_pool, sql, (const char*[]){like_pattern, NULL});
    if (!res) {
        return true;
    }

    bool enabled = true;
    if (csilk_json_array_size(res) > 0) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        enabled = (db_get_int(row, "enabled") == 1);
    }
    csilk_json_free(res);
    return enabled;
}

int
mf_scheduler_repo_job_claim_cron_run(void*       pool,
                                     int64_t     job_id,
                                     const char* expected_next_run,
                                     const char* new_next_run)
{
    if (!pool || job_id <= 0 || !expected_next_run || !new_next_run) {
        return 0;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char jid_str[32];
    snprintf(jid_str, sizeof(jid_str), "%lld", (long long)job_id);

    const char* sql =
        "UPDATE scheduled_jobs SET next_run_at = ?, last_run_status = 'running', updated_at = "
        "CURRENT_TIMESTAMP WHERE id = ? AND next_run_at = ? RETURNING id";

    csilk_json_t* res = csilk_db_query_param_json(
        db_pool, sql, (const char*[]){new_next_run, jid_str, expected_next_run, NULL});
    if (!res) {
        return 0;
    }

    int claimed = (csilk_json_array_size(res) > 0) ? 1 : 0;
    csilk_json_free(res);
    return claimed;
}

csilk_json_t*
mf_scheduler_repo_get_user_channel(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return NULL;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char*   sql = "SELECT * FROM user_notification_channels WHERE user_id = ? LIMIT 1";
    csilk_json_t* res = csilk_db_query_param_json(db_pool, sql, (const char*[]){uid_str, NULL});
    if (!res) {
        return NULL;
    }

    if (csilk_json_array_size(res) == 0) {
        csilk_json_free(res);
        return NULL;
    }

    csilk_json_t* row = csilk_json_array_get(res, 0);
    csilk_json_t* item = csilk_json_copy(row);
    csilk_json_free(res);
    return item;
}

int
mf_scheduler_repo_save_user_channel(void*       pool,
                                    int64_t     user_id,
                                    const char* channel_type,
                                    const char* channel_name,
                                    const char* webhook_url,
                                    const char* secret_key)
{
    if (!pool || user_id <= 0 || !channel_type) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    /* Check if already exists */
    csilk_json_t* existing = mf_scheduler_repo_get_user_channel(pool, user_id);
    int           rc = 0;
    if (existing) {
        csilk_json_free(existing);
        const char* sql =
            "UPDATE user_notification_channels SET channel_type = ?, channel_name = ?, webhook_url "
            "= ?, "
            "secret_key = ?, is_active = 1, updated_at = CURRENT_TIMESTAMP WHERE user_id = ?";
        rc = csilk_db_exec_param(db_pool,
                                 sql,
                                 (const char*[]){channel_type,
                                                 channel_name ? channel_name : "个人微信",
                                                 webhook_url ? webhook_url : "",
                                                 secret_key ? secret_key : "",
                                                 uid_str,
                                                 NULL});
    } else {
        const char* sql = "INSERT INTO user_notification_channels (user_id, channel_type, "
                          "channel_name, webhook_url, secret_key, is_active) "
                          "VALUES (?, ?, ?, ?, ?, 1)";
        rc = csilk_db_exec_param(db_pool,
                                 sql,
                                 (const char*[]){uid_str,
                                                 channel_type,
                                                 channel_name ? channel_name : "个人微信",
                                                 webhook_url ? webhook_url : "",
                                                 secret_key ? secret_key : "",
                                                 NULL});
    }

    return rc;
}

int64_t
mf_scheduler_repo_log_create(void*       pool,
                             int64_t     job_id,
                             int64_t     user_id,
                             const char* trigger_reason)
{
    if (!pool || job_id <= 0) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char jid_str[32], uid_str[32];
    snprintf(jid_str, sizeof(jid_str), "%lld", (long long)job_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char* sql =
        "INSERT INTO job_run_logs (job_id, user_id, status, trigger_reason, notification_status) "
        "VALUES (?, ?, 'running', ?, 'none') RETURNING id";

    csilk_json_t* res = csilk_db_query_param_json(
        db_pool,
        sql,
        (const char*[]){jid_str, uid_str, trigger_reason ? trigger_reason : "manual_invoke", NULL});
    if (!res) {
        return -1;
    }

    int64_t new_id = -1;
    if (csilk_json_array_size(res) > 0) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        new_id = db_get_int(row, "id");
    }
    csilk_json_free(res);
    return new_id;
}

int
mf_scheduler_repo_log_complete(void*       pool,
                               int64_t     log_id,
                               const char* status,
                               int         duration_ms,
                               const char* output_summary,
                               const char* error_msg,
                               const char* notif_status)
{
    if (!pool || log_id <= 0) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char lid_str[32], dur_str[32], now_str[64];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)log_id);
    snprintf(dur_str, sizeof(dur_str), "%d", duration_ms);
    get_now_str(now_str, sizeof(now_str));

    const char* sql = "UPDATE job_run_logs SET completed_at = ?, status = ?, duration_ms = ?, "
                      "output_summary = ?, "
                      "error_message = ?, notification_status = ? WHERE id = ?";

    return csilk_db_exec_param(db_pool,
                               sql,
                               (const char*[]){now_str,
                                               status ? status : "success",
                                               dur_str,
                                               output_summary ? output_summary : "",
                                               error_msg ? error_msg : "",
                                               notif_status ? notif_status : "none",
                                               lid_str,
                                               NULL});
}

csilk_json_t*
mf_scheduler_repo_log_list(void*    pool,
                           int64_t  user_id,
                           bool     is_admin,
                           int64_t  job_id,
                           int64_t  page,
                           int64_t  page_size,
                           int64_t* out_total)
{
    if (!pool) {
        return NULL;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 20;
    }
    int64_t offset = (page - 1) * page_size;

    char uid_str[32], jid_str[32], limit_str[32], offset_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(jid_str, sizeof(jid_str), "%lld", (long long)job_id);
    snprintf(limit_str, sizeof(limit_str), "%lld", (long long)page_size);
    snprintf(offset_str, sizeof(offset_str), "%lld", (long long)offset);

    char        count_sql[256];
    char        list_sql[256];
    const char* count_params[4];
    const char* list_params[5];

    if (job_id > 0) {
        if (is_admin) {
            snprintf(count_sql,
                     sizeof(count_sql),
                     "SELECT COUNT(*) as total FROM job_run_logs WHERE job_id = ?");
            count_params[0] = jid_str;
            count_params[1] = NULL;

            snprintf(list_sql,
                     sizeof(list_sql),
                     "SELECT * FROM job_run_logs WHERE job_id = ? ORDER BY started_at DESC, id "
                     "DESC LIMIT ? OFFSET ?");
            list_params[0] = jid_str;
            list_params[1] = limit_str;
            list_params[2] = offset_str;
            list_params[3] = NULL;
        } else {
            snprintf(count_sql,
                     sizeof(count_sql),
                     "SELECT COUNT(*) as total FROM job_run_logs WHERE job_id = ? AND user_id = ?");
            count_params[0] = jid_str;
            count_params[1] = uid_str;
            count_params[2] = NULL;

            snprintf(list_sql,
                     sizeof(list_sql),
                     "SELECT * FROM job_run_logs WHERE job_id = ? AND user_id = ? ORDER BY "
                     "started_at DESC, id DESC LIMIT ? OFFSET ?");
            list_params[0] = jid_str;
            list_params[1] = uid_str;
            list_params[2] = limit_str;
            list_params[3] = offset_str;
            list_params[4] = NULL;
        }
    } else {
        if (is_admin) {
            snprintf(count_sql, sizeof(count_sql), "SELECT COUNT(*) as total FROM job_run_logs");
            count_params[0] = NULL;

            snprintf(
                list_sql,
                sizeof(list_sql),
                "SELECT * FROM job_run_logs ORDER BY started_at DESC, id DESC LIMIT ? OFFSET ?");
            list_params[0] = limit_str;
            list_params[1] = offset_str;
            list_params[2] = NULL;
        } else {
            snprintf(count_sql,
                     sizeof(count_sql),
                     "SELECT COUNT(*) as total FROM job_run_logs WHERE user_id = ?");
            count_params[0] = uid_str;
            count_params[1] = NULL;

            snprintf(list_sql,
                     sizeof(list_sql),
                     "SELECT * FROM job_run_logs WHERE user_id = ? ORDER BY started_at DESC, id "
                     "DESC LIMIT ? OFFSET ?");
            list_params[0] = uid_str;
            list_params[1] = limit_str;
            list_params[2] = offset_str;
            list_params[3] = NULL;
        }
    }

    if (out_total) {
        *out_total = 0;
        csilk_json_t* cres = count_params[0] != NULL
                                 ? csilk_db_query_param_json(db_pool, count_sql, count_params)
                                 : csilk_db_query_json(db_pool, count_sql);
        if (cres) {
            if (csilk_json_array_size(cres) > 0) {
                csilk_json_t* crow = csilk_json_array_get(cres, 0);
                *out_total = db_get_int(crow, "total");
            }
            csilk_json_free(cres);
        }
    }

    return csilk_db_query_param_json(db_pool, list_sql, list_params);
}

int64_t
scheduler_repo_count_active(void* pool, int64_t user_id)
{
    if (!pool) {
        return 0;
    }
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* sql =
        "SELECT COUNT(*) as cnt FROM scheduled_jobs WHERE enabled = 1 AND (user_id = ? "
        "OR is_builtin = 1)";
    int64_t       active_jobs = 0;
    csilk_json_t* ares =
        csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, (const char*[]){uid_str, NULL});
    if (ares) {
        if (csilk_json_array_size(ares) > 0) {
            active_jobs = db_get_int(csilk_json_array_get(ares, 0), "cnt");
        }
        csilk_json_free(ares);
    }
    return active_jobs;
}

int64_t
scheduler_repo_count_runs_today(void* pool, int64_t user_id, const char* today_start)
{
    if (!pool) {
        return 0;
    }
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* sql = "SELECT COUNT(*) as cnt FROM job_run_logs WHERE (user_id = ? OR user_id = 0) "
                      "AND started_at >= ?";
    int64_t     today_runs = 0;
    csilk_json_t* rres = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool, sql, (const char*[]){uid_str, today_start, NULL});
    if (rres) {
        if (csilk_json_array_size(rres) > 0) {
            today_runs = db_get_int(csilk_json_array_get(rres, 0), "cnt");
        }
        csilk_json_free(rres);
    }
    return today_runs;
}

int
scheduler_repo_get_next_run(void*       pool,
                            int64_t     user_id,
                            const char* now_str,
                            char*       out_name,
                            size_t      name_sz,
                            char*       out_time,
                            size_t      time_sz)
{
    if (!pool) {
        return -1;
    }
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* sql =
        "SELECT name, next_run_at FROM scheduled_jobs WHERE enabled = 1 AND (user_id = "
        "? OR is_builtin = 1) AND next_run_at > ? ORDER BY next_run_at ASC LIMIT 1";
    csilk_json_t* nres = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool, sql, (const char*[]){uid_str, now_str, NULL});
    if (nres) {
        if (csilk_json_array_size(nres) > 0) {
            csilk_json_t* nrow = csilk_json_array_get(nres, 0);
            const char*   sname = csilk_json_get_string(nrow, "name");
            const char*   stime = csilk_json_get_string(nrow, "next_run_at");
            if (sname && out_name && name_sz > 0) {
                snprintf(out_name, name_sz, "%s", sname);
            }
            if (stime && out_time && time_sz > 0) {
                snprintf(out_time, time_sz, "%s", stime);
            }
        }
        csilk_json_free(nres);
    }
    return 0;
}

csilk_json_t*
scheduler_repo_list_enabled_by_trigger(void* pool, const char* trigger_type)
{
    if (!pool || !trigger_type || !trigger_type[0]) {
        return NULL;
    }
    const char* sql = "SELECT id, user_id, trigger_config, next_run_at FROM scheduled_jobs "
                      "WHERE trigger_type = ? AND enabled = 1";
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool, sql, (const char*[]){trigger_type, NULL});
}
