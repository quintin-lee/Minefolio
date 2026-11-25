/** @file im_push_outbox_repo_impl.c @brief iLink 定时推送持久化 Outbox 仓储实现 */

#include "infrastructure/repositories/im_push_outbox_repo_impl.h"
#include "common/db.h"
#include "infrastructure/repositories/scheduler_repo_impl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
push_i64_str(int64_t v, char* buf, size_t cap)
{
    snprintf(buf, cap, "%lld", (long long)v);
}

static bool
push_is_postgres(csilk_db_pool_t* pool)
{
    const char* drv = pool ? csilk_db_pool_get_driver_name(pool) : NULL;
    return drv && strcmp(drv, "postgres") == 0;
}

static bool
push_status_is_terminal(const char* st)
{
    if (!st) {
        return false;
    }
    return strcmp(st, "sent") == 0 || strcmp(st, "failed") == 0 || strcmp(st, "expired") == 0;
}

static int64_t
push_backoff_seconds(int attempts_after_inc, int retry_after_sec)
{
    if (retry_after_sec > 0) {
        return retry_after_sec > 300 ? 300 : retry_after_sec;
    }
    static const int64_t kBackoff[] = {10, 30, 60, 120};
    int                  idx = attempts_after_inc - 1;
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= (int)(sizeof(kBackoff) / sizeof(kBackoff[0]))) {
        idx = (int)(sizeof(kBackoff) / sizeof(kBackoff[0])) - 1;
    }
    return kBackoff[idx];
}

int64_t
mf_im_push_outbox_enqueue(csilk_db_pool_t* pool,
                          int64_t          instance_id,
                          int64_t          mf_user_id,
                          int64_t          job_run_log_id,
                          const char*      kind,
                          const char*      title,
                          const char*      content,
                          int64_t          ttl_sec,
                          int              max_attempts)
{
    if (!pool || instance_id <= 0 || mf_user_id <= 0 || !title || !title[0] || !content) {
        return -1;
    }
    const char* eff_kind = (kind && strcmp(kind, "test") == 0) ? "test" : "scheduler";
    int64_t     eff_ttl = ttl_sec > 0 ? ttl_sec : 1800;
    int         eff_max = max_attempts > 0 ? max_attempts : 5;
    int64_t     now = (int64_t)time(NULL);
    int64_t     expires_at = now + eff_ttl;

    char inst_s[32], uid_s[32], log_s[32], max_s[16], next_s[32], exp_s[32];
    push_i64_str(instance_id, inst_s, sizeof(inst_s));
    push_i64_str(mf_user_id, uid_s, sizeof(uid_s));
    push_i64_str(job_run_log_id, log_s, sizeof(log_s));
    snprintf(max_s, sizeof(max_s), "%d", eff_max);
    push_i64_str(now, next_s, sizeof(next_s));
    push_i64_str(expires_at, exp_s, sizeof(exp_s));

    csilk_json_t* res = NULL;
    if (job_run_log_id > 0) {
        const char* sql =
            "INSERT INTO im_push_outbox "
            "(instance_id, mf_user_id, job_run_log_id, kind, title, content, "
            " status, attempts, max_attempts, lease_until_ts, next_attempt_at_ts, expires_at_ts) "
            "VALUES (?, ?, ?, ?, ?, ?, 'pending', 0, ?, 0, ?, ?) RETURNING id";
        res = csilk_db_query_param_json(
            pool,
            sql,
            (const char*[]){
                inst_s, uid_s, log_s, eff_kind, title, content, max_s, next_s, exp_s, NULL});
    } else {
        const char* sql =
            "INSERT INTO im_push_outbox "
            "(instance_id, mf_user_id, job_run_log_id, kind, title, content, "
            " status, attempts, max_attempts, lease_until_ts, next_attempt_at_ts, expires_at_ts) "
            "VALUES (?, ?, NULL, ?, ?, ?, 'pending', 0, ?, 0, ?, ?) RETURNING id";
        res = csilk_db_query_param_json(
            pool,
            sql,
            (const char*[]){inst_s, uid_s, eff_kind, title, content, max_s, next_s, exp_s, NULL});
    }

    int64_t id = -1;
    if (res && csilk_json_is_array(res) && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id > 0 ? id : -1;
}

csilk_json_t*
mf_im_push_outbox_get(csilk_db_pool_t* pool, int64_t push_id)
{
    if (!pool || push_id <= 0) {
        return NULL;
    }
    char pid_s[32];
    push_i64_str(push_id, pid_s, sizeof(pid_s));
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT id, instance_id, mf_user_id, job_run_log_id, kind, title, content, status, "
        "attempts, max_attempts, lease_until_ts, next_attempt_at_ts, expires_at_ts, last_error, "
        "CAST(created_at AS TEXT) AS created_at, CAST(updated_at AS TEXT) AS updated_at "
        "FROM im_push_outbox WHERE id=? LIMIT 1",
        (const char*[]){pid_s, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return NULL;
    }
    csilk_json_t* copy = csilk_json_copy(csilk_json_array_get(res, 0));
    csilk_json_free(res);
    return copy;
}

int
mf_im_push_outbox_mark_expired_by_id(csilk_db_pool_t* pool, int64_t push_id, const char* reason)
{
    if (!pool || push_id <= 0) {
        return -1;
    }
    char pid_s[32];
    push_i64_str(push_id, pid_s, sizeof(pid_s));
    const char* err = (reason && reason[0]) ? reason : "推送等待超时已取消";
    return csilk_db_exec_param(
        pool,
        "UPDATE im_push_outbox SET status='expired', lease_until_ts=0, last_error=?, "
        "updated_at=CURRENT_TIMESTAMP WHERE id=? AND status IN ('pending','leased')",
        (const char*[]){err, pid_s, NULL});
}

int
mf_im_push_outbox_expire_stale(csilk_db_pool_t* pool,
                               int64_t          instance_id_or_zero,
                               int64_t          now_ts,
                               const char*      default_reason,
                               int64_t**        out_log_ids,
                               size_t*          out_log_count)
{
    if (out_log_ids) {
        *out_log_ids = NULL;
    }
    if (out_log_count) {
        *out_log_count = 0;
    }
    if (!pool) {
        return -1;
    }
    int64_t     now = now_ts > 0 ? now_ts : (int64_t)time(NULL);
    const char* reason = (default_reason && default_reason[0])
                             ? default_reason
                             : "超过30分钟等待窗口（网关离线或微信会话未激活），推送已过期";

    char now_s[32], inst_s[32];
    push_i64_str(now, now_s, sizeof(now_s));
    push_i64_str(instance_id_or_zero, inst_s, sizeof(inst_s));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_push_expire", &scope) != 0) {
        return -1;
    }

    csilk_json_t* logs_res = NULL;
    if (instance_id_or_zero > 0) {
        logs_res = csilk_db_query_param_json(
            pool,
            "SELECT DISTINCT job_run_log_id FROM im_push_outbox "
            "WHERE instance_id=? AND status IN ('pending','leased') AND expires_at_ts <= ? "
            "AND job_run_log_id IS NOT NULL",
            (const char*[]){inst_s, now_s, NULL});
    } else {
        logs_res =
            csilk_db_query_param_json(pool,
                                      "SELECT DISTINCT job_run_log_id FROM im_push_outbox "
                                      "WHERE status IN ('pending','leased') AND expires_at_ts <= ? "
                                      "AND job_run_log_id IS NOT NULL",
                                      (const char*[]){now_s, NULL});
    }
    if (!logs_res || !csilk_json_is_array(logs_res)) {
        if (logs_res) {
            csilk_json_free(logs_res);
        }
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }

    int rc = 0;
    if (instance_id_or_zero > 0) {
        rc = csilk_db_exec_param(
            pool,
            "UPDATE im_push_outbox SET status='expired', lease_until_ts=0, "
            "last_error=CASE WHEN last_error='' THEN ? ELSE last_error END, "
            "updated_at=CURRENT_TIMESTAMP "
            "WHERE instance_id=? AND status IN ('pending','leased') AND expires_at_ts <= ?",
            (const char*[]){reason, inst_s, now_s, NULL});
    } else {
        rc = csilk_db_exec_param(pool,
                                 "UPDATE im_push_outbox SET status='expired', lease_until_ts=0, "
                                 "last_error=CASE WHEN last_error='' THEN ? ELSE last_error END, "
                                 "updated_at=CURRENT_TIMESTAMP "
                                 "WHERE status IN ('pending','leased') AND expires_at_ts <= ?",
                                 (const char*[]){reason, now_s, NULL});
    }
    if (rc != 0) {
        csilk_json_free(logs_res);
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (db_tx_scope_commit(pool, &scope) != 0) {
        csilk_json_free(logs_res);
        return -1;
    }

    if (out_log_ids && out_log_count) {
        size_t n = csilk_json_array_size(logs_res);
        if (n > 0) {
            int64_t* ids = (int64_t*)calloc(n, sizeof(int64_t));
            if (ids) {
                size_t k = 0;
                for (size_t i = 0; i < n; ++i) {
                    int64_t lid = db_get_int(csilk_json_array_get(logs_res, i), "job_run_log_id");
                    if (lid > 0) {
                        ids[k++] = lid;
                    }
                }
                if (k > 0) {
                    *out_log_ids = ids;
                    *out_log_count = k;
                } else {
                    free(ids);
                }
            }
        }
    }
    csilk_json_free(logs_res);
    return 0;
}

csilk_json_t*
mf_im_push_outbox_claim(csilk_db_pool_t* pool,
                        int64_t          instance_id,
                        bool             has_context,
                        int              limit,
                        int64_t          now_ts,
                        int64_t          lease_sec)
{
    if (!pool || instance_id <= 0) {
        return NULL;
    }
    int eff_limit = limit;
    if (eff_limit < 1) {
        eff_limit = 5;
    }
    if (eff_limit > 10) {
        eff_limit = 10;
    }
    int64_t now = now_ts > 0 ? now_ts : (int64_t)time(NULL);
    int64_t lsec = lease_sec > 0 ? lease_sec : 60;
    int64_t lease_until = now + lsec;

    char inst_s[32], now_s[32], lim_s[16], lease_s[32];
    push_i64_str(instance_id, inst_s, sizeof(inst_s));
    push_i64_str(now, now_s, sizeof(now_s));
    snprintf(lim_s, sizeof(lim_s), "%d", eff_limit);
    push_i64_str(lease_until, lease_s, sizeof(lease_s));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_push_claim", &scope) != 0) {
        return NULL;
    }

    if (csilk_db_exec_param(pool,
                            "UPDATE im_push_outbox SET status='pending', lease_until_ts=0, "
                            "updated_at=CURRENT_TIMESTAMP "
                            "WHERE instance_id=? AND status='leased' AND lease_until_ts <= ?",
                            (const char*[]){inst_s, now_s, NULL}) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return NULL;
    }

    char sql[768];
    SQL_FMT(sql,
            sizeof(sql),
            "SELECT id, kind, title, content, attempts, max_attempts FROM im_push_outbox "
            "WHERE instance_id=? AND status='pending' AND next_attempt_at_ts <= ? "
            "AND expires_at_ts > ?%s "
            "ORDER BY CASE WHEN kind='test' THEN 0 ELSE 1 END, id ASC LIMIT ?%s",
            has_context ? "" : " AND kind='test'",
            push_is_postgres(pool) ? " FOR UPDATE SKIP LOCKED" : "");

    csilk_json_t* cand =
        csilk_db_query_param_json(pool, sql, (const char*[]){inst_s, now_s, now_s, lim_s, NULL});
    if (!cand || !csilk_json_is_array(cand)) {
        if (cand) {
            csilk_json_free(cand);
        }
        db_tx_scope_rollback(pool, &scope);
        return NULL;
    }

    csilk_json_t* claimed = csilk_json_array();
    if (!claimed) {
        csilk_json_free(cand);
        db_tx_scope_rollback(pool, &scope);
        return NULL;
    }

    size_t n = csilk_json_array_size(cand);
    for (size_t i = 0; i < n; ++i) {
        csilk_json_t* row = csilk_json_array_get(cand, i);
        int64_t       pid = db_get_int(row, "id");
        if (pid <= 0) {
            continue;
        }
        char pid_s[32];
        push_i64_str(pid, pid_s, sizeof(pid_s));
        csilk_json_t* upd = csilk_db_query_param_json(
            pool,
            "UPDATE im_push_outbox SET status='leased', lease_until_ts=?, "
            "updated_at=CURRENT_TIMESTAMP WHERE id=? AND instance_id=? AND status='pending' "
            "RETURNING id",
            (const char*[]){lease_s, pid_s, inst_s, NULL});
        if (!upd) {
            csilk_json_free(cand);
            csilk_json_free(claimed);
            db_tx_scope_rollback(pool, &scope);
            return NULL;
        }
        bool ok = csilk_json_is_array(upd) && csilk_json_array_size(upd) > 0;
        csilk_json_free(upd);
        if (ok) {
            csilk_json_t* item = csilk_json_object();
            if (!item) {
                csilk_json_free(cand);
                csilk_json_free(claimed);
                db_tx_scope_rollback(pool, &scope);
                return NULL;
            }
            csilk_json_add_int(item, "id", pid);
            const char* k = csilk_json_get_string(row, "kind");
            const char* t = csilk_json_get_string(row, "title");
            const char* c = csilk_json_get_string(row, "content");
            csilk_json_add_string(item, "kind", k ? k : "scheduler");
            csilk_json_add_string(item, "title", t ? t : "");
            csilk_json_add_string(item, "content", c ? c : "");
            csilk_json_add_int(item, "attempts", db_get_int(row, "attempts"));
            csilk_json_add_int(item, "max_attempts", db_get_int(row, "max_attempts"));
            csilk_json_array_append(claimed, item);
        }
    }
    csilk_json_free(cand);

    if (db_tx_scope_commit(pool, &scope) != 0) {
        csilk_json_free(claimed);
        return NULL;
    }
    return claimed;
}

int
mf_im_push_outbox_ack(csilk_db_pool_t* pool,
                      int64_t          instance_id,
                      int64_t          push_id,
                      const char*      result,
                      const char*      error_msg,
                      int              retry_after_sec,
                      int64_t          now_ts,
                      int64_t*         out_job_run_log_id)
{
    if (out_job_run_log_id) {
        *out_job_run_log_id = 0;
    }
    if (!pool || instance_id <= 0 || push_id <= 0 || !result || !result[0]) {
        return -2;
    }
    bool is_sent = strcmp(result, "sent") == 0;
    bool is_wait_ctx = strcmp(result, "waiting_context") == 0;
    bool is_retry = strcmp(result, "retry") == 0;
    bool is_failed = strcmp(result, "failed") == 0;
    if (!is_sent && !is_wait_ctx && !is_retry && !is_failed) {
        return -2;
    }

    int64_t now = now_ts > 0 ? now_ts : (int64_t)time(NULL);
    char    pid_s[32], inst_s[32];
    push_i64_str(push_id, pid_s, sizeof(pid_s));
    push_i64_str(instance_id, inst_s, sizeof(inst_s));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_push_ack", &scope) != 0) {
        return -1;
    }

    char sql[512];
    SQL_FMT(sql,
            sizeof(sql),
            "SELECT kind, status, attempts, max_attempts, expires_at_ts, "
            "COALESCE(job_run_log_id, 0) AS job_run_log_id "
            "FROM im_push_outbox WHERE id=? AND instance_id=? LIMIT 1%s",
            push_is_postgres(pool) ? " FOR UPDATE" : "");
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){pid_s, inst_s, NULL});
    if (!res) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (!csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        csilk_json_free(res);
        db_tx_scope_rollback(pool, &scope);
        return -3;
    }

    csilk_json_t* row = csilk_json_array_get(res, 0);
    const char*   cur_status = csilk_json_get_string(row, "status");
    const char*   kind = csilk_json_get_string(row, "kind");
    bool          is_test = kind && strcmp(kind, "test") == 0;
    int           attempts = (int)db_get_int(row, "attempts");
    int           max_attempts = (int)db_get_int(row, "max_attempts");
    if (max_attempts <= 0) {
        max_attempts = 5;
    }
    int64_t expires_at = db_get_int(row, "expires_at_ts");
    int64_t log_id = db_get_int(row, "job_run_log_id");
    if (out_job_run_log_id) {
        *out_job_run_log_id = log_id;
    }

    if (push_status_is_terminal(cur_status)) {
        csilk_json_free(res);
        return db_tx_scope_commit(pool, &scope) == 0 ? 0 : -1;
    }

    const char* new_status = "failed";
    int         new_attempts = attempts;
    int64_t     next_attempt = now;
    const char* new_err = error_msg ? error_msg : "";

    if (is_sent) {
        new_status = "sent";
        new_err = "";
    } else if (is_wait_ctx) {
        if (is_test) {
            new_status = "failed";
            if (!new_err[0]) {
                new_err = "请先在微信向机器人发送任意一条消息以建立会话，再重新测试";
            }
        } else if (expires_at <= now) {
            new_status = "expired";
            if (!new_err[0]) {
                new_err = "超过30分钟等待窗口（微信会话未激活），推送已过期";
            }
        } else {
            new_status = "pending";
            next_attempt = now + 30;
            if (!new_err[0]) {
                new_err = "等待微信会话上下文刷新（请在微信给机器人发送任意消息）";
            }
        }
    } else if (is_retry) {
        new_attempts = attempts + 1;
        if (is_test) {
            new_status = "failed";
        } else if (expires_at <= now && new_attempts < max_attempts) {
            new_status = "expired";
        } else if (new_attempts >= max_attempts) {
            new_status = "failed";
        } else {
            new_status = "pending";
            next_attempt = now + push_backoff_seconds(new_attempts, retry_after_sec);
        }
    } else {
        new_status = "failed";
    }

    char att_s[16], next_s[32];
    snprintf(att_s, sizeof(att_s), "%d", new_attempts);
    push_i64_str(next_attempt, next_s, sizeof(next_s));

    int urc = csilk_db_exec_param(
        pool,
        "UPDATE im_push_outbox SET status=?, attempts=?, lease_until_ts=0, "
        "next_attempt_at_ts=?, last_error=?, updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND instance_id=?",
        (const char*[]){new_status, att_s, next_s, new_err, pid_s, inst_s, NULL});
    csilk_json_free(res);
    if (urc != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (db_tx_scope_commit(pool, &scope) != 0) {
        return -1;
    }
    return 0;
}

int
mf_im_push_outbox_rollup_job_log(csilk_db_pool_t* pool, int64_t job_run_log_id)
{
    if (!pool || job_run_log_id <= 0) {
        return -1;
    }
    char lid_s[32];
    push_i64_str(job_run_log_id, lid_s, sizeof(lid_s));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_push_rollup", &scope) != 0) {
        return -1;
    }

    char lsql[256];
    SQL_FMT(lsql,
            sizeof(lsql),
            "SELECT notif_sync_sent, notif_sync_failed, error_message "
            "FROM job_run_logs WHERE id=? LIMIT 1%s",
            push_is_postgres(pool) ? " FOR UPDATE" : "");
    csilk_json_t* lres = csilk_db_query_param_json(pool, lsql, (const char*[]){lid_s, NULL});
    if (!lres) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (!csilk_json_is_array(lres) || csilk_json_array_size(lres) == 0) {
        csilk_json_free(lres);
        return db_tx_scope_commit(pool, &scope) == 0 ? 0 : -1;
    }
    csilk_json_t* lrow = csilk_json_array_get(lres, 0);
    int           sync_sent = (int)db_get_int(lrow, "notif_sync_sent");
    int           sync_failed = (int)db_get_int(lrow, "notif_sync_failed");
    const char*   existing_err = csilk_json_get_string(lrow, "error_message");
    char          err_buf[512];
    err_buf[0] = '\0';
    if (existing_err && existing_err[0]) {
        snprintf(err_buf, sizeof(err_buf), "%s", existing_err);
    }
    csilk_json_free(lres);

    csilk_json_t* ores = csilk_db_query_param_json(
        pool,
        "SELECT status, last_error FROM im_push_outbox WHERE job_run_log_id=?",
        (const char*[]){lid_s, NULL});
    if (!ores || !csilk_json_is_array(ores)) {
        if (ores) {
            csilk_json_free(ores);
        }
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }

    int  out_pending = 0, out_sent = 0, out_failed = 0;
    char out_err[512];
    out_err[0] = '\0';
    size_t n = csilk_json_array_size(ores);
    for (size_t i = 0; i < n; ++i) {
        csilk_json_t* r = csilk_json_array_get(ores, i);
        const char*   st = csilk_json_get_string(r, "status");
        const char*   le = csilk_json_get_string(r, "last_error");
        if (!st) {
            continue;
        }
        if (strcmp(st, "sent") == 0) {
            out_sent++;
        } else if (strcmp(st, "failed") == 0 || strcmp(st, "expired") == 0) {
            out_failed++;
            if (!out_err[0] && le && le[0]) {
                snprintf(out_err, sizeof(out_err), "%s", le);
            }
        } else {
            out_pending++;
        }
    }
    csilk_json_free(ores);

    int         ok_total = sync_sent + out_sent;
    int         fail_total = sync_failed + out_failed;
    const char* final_status = "none";
    if (out_pending > 0) {
        final_status = "queued";
    } else if (ok_total > 0 && fail_total > 0) {
        final_status = "partial";
    } else if (ok_total > 0) {
        final_status = "sent";
    } else if (fail_total > 0) {
        final_status = "failed";
    }

    if (fail_total == 0 && out_pending == 0) {
        err_buf[0] = '\0';
    } else if (!err_buf[0] && out_err[0]) {
        snprintf(err_buf, sizeof(err_buf), "%s", out_err);
    }

    if (mf_scheduler_repo_log_update_notif(pool, job_run_log_id, final_status, err_buf) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (db_tx_scope_commit(pool, &scope) != 0) {
        return -1;
    }
    return 0;
}
