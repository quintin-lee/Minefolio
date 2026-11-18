#include "application/scheduler/usecases.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common/db.h"
#include "core/scheduler/cron_parser.h"
#include "core/scheduler/scheduler_engine.h"
#include "infrastructure/repositories/scheduler_repo_impl.h"
#include "services/notification/wechat_push_service.h"

static void
get_today_start_str(char* buf, size_t len)
{
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, len, "%Y-%m-%d 00:00:00", &tm_now);
}

static void
get_now_str(char* buf, size_t len)
{
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_now);
}

int
scheduler_usecase_dashboard(void* pool, int64_t user_id, bool is_admin, csilk_json_t** out_resp)
{
    if (!pool || !out_resp) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    char uid_str[32], today_start[64], now_str[64];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    get_today_start_str(today_start, sizeof(today_start));
    get_now_str(now_str, sizeof(now_str));

    /* 1. Active jobs count */
    const char* active_sql =
        is_admin ? "SELECT COUNT(*) as cnt FROM scheduled_jobs WHERE enabled = 1 AND (user_id = ? "
                   "OR user_id = 0)"
                 : "SELECT COUNT(*) as cnt FROM scheduled_jobs WHERE enabled = 1 AND user_id = ?";
    int64_t       active_jobs = 0;
    csilk_json_t* ares =
        csilk_db_query_param_json(db_pool, active_sql, (const char*[]){uid_str, NULL});
    if (ares) {
        if (csilk_json_array_size(ares) > 0) {
            active_jobs = db_get_int(csilk_json_array_get(ares, 0), "cnt");
        }
        csilk_json_free(ares);
    }

    /* 2. Today's run count */
    const char* runs_sql =
        is_admin ? "SELECT COUNT(*) as cnt FROM job_run_logs WHERE (user_id = ? OR user_id = 0) "
                   "AND started_at >= ?"
                 : "SELECT COUNT(*) as cnt FROM job_run_logs WHERE user_id = ? AND started_at >= ?";
    int64_t       today_runs = 0;
    csilk_json_t* rres =
        csilk_db_query_param_json(db_pool, runs_sql, (const char*[]){uid_str, today_start, NULL});
    if (rres) {
        if (csilk_json_array_size(rres) > 0) {
            today_runs = db_get_int(csilk_json_array_get(rres, 0), "cnt");
        }
        csilk_json_free(rres);
    }

    /* 3. Nearest next run job */
    const char* next_sql =
        is_admin ? "SELECT name, next_run_at FROM scheduled_jobs WHERE enabled = 1 AND (user_id = "
                   "? OR user_id = 0) AND next_run_at > ? ORDER BY next_run_at ASC LIMIT 1"
                 : "SELECT name, next_run_at FROM scheduled_jobs WHERE enabled = 1 AND user_id = ? "
                   "AND next_run_at > ? ORDER BY next_run_at ASC LIMIT 1";
    char          next_job_name[128] = "";
    char          next_job_time[64] = "";
    csilk_json_t* nres =
        csilk_db_query_param_json(db_pool, next_sql, (const char*[]){uid_str, now_str, NULL});
    if (nres) {
        if (csilk_json_array_size(nres) > 0) {
            csilk_json_t* nrow = csilk_json_array_get(nres, 0);
            const char*   sname = csilk_json_get_string(nrow, "name");
            const char*   stime = csilk_json_get_string(nrow, "next_run_at");
            if (sname) {
                snprintf(next_job_name, sizeof(next_job_name), "%s", sname);
            }
            if (stime) {
                snprintf(next_job_time, sizeof(next_job_time), "%s", stime);
            }
        }
        csilk_json_free(nres);
    }

    /* 4. Notification channel status */
    csilk_json_t* ch = mf_scheduler_repo_get_user_channel(pool, user_id);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "active_jobs", (double)active_jobs);
    csilk_json_add_number(resp, "today_runs", (double)today_runs);

    csilk_json_t* next_obj = csilk_json_object();
    csilk_json_add_string(next_obj, "name", next_job_name);
    csilk_json_add_string(next_obj, "next_run_at", next_job_time);
    csilk_json_add_object(resp, "next_run_job", next_obj);

    csilk_json_t* ch_obj = csilk_json_object();
    if (ch) {
        csilk_json_add_string(ch_obj, "channel_type", csilk_json_get_string(ch, "channel_type"));
        csilk_json_add_string(ch_obj, "channel_name", csilk_json_get_string(ch, "channel_name"));
        csilk_json_add_number(ch_obj, "is_active", (double)db_get_int(ch, "is_active"));
        csilk_json_free(ch);
    } else {
        csilk_json_add_string(ch_obj, "channel_type", "none");
        csilk_json_add_string(ch_obj, "channel_name", "");
        csilk_json_add_number(ch_obj, "is_active", 0);
    }
    csilk_json_add_object(resp, "push_channel", ch_obj);

    *out_resp = resp;
    return 0;
}

int
scheduler_usecase_job_list(void*          pool,
                           int64_t        user_id,
                           bool           is_admin,
                           int64_t        page,
                           int64_t        page_size,
                           csilk_json_t** out_resp)
{
    if (!pool || !out_resp) {
        return -1;
    }

    int64_t       total = 0;
    csilk_json_t* list =
        mf_scheduler_repo_job_list(pool, user_id, is_admin, page, page_size, &total);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_array(resp, "items", list ? list : csilk_json_array());
    csilk_json_add_number(resp, "total", (double)total);
    csilk_json_add_number(resp, "page", (double)page);
    csilk_json_add_number(resp, "page_size", (double)page_size);

    *out_resp = resp;
    return 0;
}

int
scheduler_usecase_job_get(
    void* pool, int64_t user_id, bool is_admin, int64_t job_id, csilk_json_t** out_job)
{
    if (!pool || !out_job || job_id <= 0) {
        return -1;
    }

    csilk_json_t* job = mf_scheduler_repo_job_get(pool, user_id, is_admin, job_id);
    if (!job) {
        return 1; /* Not found */
    }

    *out_job = job;
    return 0;
}

int
scheduler_usecase_job_create(void*                   pool,
                             int64_t                 user_id,
                             const job_create_dto_t* dto,
                             csilk_json_t**          out_job)
{
    if (!pool || !dto || !dto->name[0] || !dto->job_type[0] || !dto->trigger_type[0]) {
        return -1;
    }

    char next_run_buf[64];
    next_run_buf[0] = '\0';

    if (strcmp(dto->trigger_type, "cron") == 0 && dto->trigger_config[0]) {
        csilk_json_t* tjson = csilk_json_parse(dto->trigger_config);
        if (tjson) {
            const char* cron_s = csilk_json_get_string(tjson, "cron");
            if (cron_s && *cron_s) {
                cron_format_next_str(cron_s, time(NULL), next_run_buf, sizeof(next_run_buf));
            }
            csilk_json_free(tjson);
        }
    }

    int64_t new_id = mf_scheduler_repo_job_create(pool,
                                                  user_id,
                                                  dto->name,
                                                  dto->description,
                                                  dto->job_type,
                                                  dto->trigger_type,
                                                  dto->trigger_config,
                                                  dto->payload_config,
                                                  dto->delivery_channel,
                                                  dto->delivery_config,
                                                  next_run_buf);
    if (new_id <= 0) {
        return -1;
    }

    scheduler_engine_notify_reload();

    if (out_job) {
        *out_job = mf_scheduler_repo_job_get(pool, user_id, false, new_id);
    }
    return 0;
}

int
scheduler_usecase_job_update(
    void* pool, int64_t user_id, bool is_admin, int64_t job_id, const job_update_dto_t* dto)
{
    if (!pool || !dto || job_id <= 0) {
        return -1;
    }

    csilk_json_t* existing = mf_scheduler_repo_job_get(pool, user_id, is_admin, job_id);
    if (!existing) {
        return 1004; /* Forbidden / Not owner */
    }

    int64_t job_user_id = db_get_int(existing, "user_id");
    csilk_json_free(existing);

    if (job_user_id == 0 && !is_admin) {
        return 1004; /* Built-in jobs can only be updated by admin */
    }

    char next_run_buf[64];
    next_run_buf[0] = '\0';

    if (strcmp(dto->trigger_type, "cron") == 0 && dto->trigger_config[0]) {
        csilk_json_t* tjson = csilk_json_parse(dto->trigger_config);
        if (tjson) {
            const char* cron_s = csilk_json_get_string(tjson, "cron");
            if (cron_s && *cron_s) {
                cron_format_next_str(cron_s, time(NULL), next_run_buf, sizeof(next_run_buf));
            }
            csilk_json_free(tjson);
        }
    }

    int rc = mf_scheduler_repo_job_update(pool,
                                          user_id,
                                          is_admin,
                                          job_id,
                                          dto->name,
                                          dto->description,
                                          dto->trigger_type,
                                          dto->trigger_config,
                                          dto->payload_config,
                                          dto->delivery_channel,
                                          dto->delivery_config,
                                          next_run_buf);
    if (rc != 0) {
        return -1;
    }

    scheduler_engine_notify_reload();
    return 0;
}

int
scheduler_usecase_job_delete(void* pool, int64_t user_id, int64_t job_id)
{
    if (!pool || job_id <= 0) {
        return -1;
    }

    csilk_json_t* existing = mf_scheduler_repo_job_get(pool, user_id, true, job_id);
    if (!existing) {
        return 1004;
    }

    int64_t job_user_id = db_get_int(existing, "user_id");
    csilk_json_free(existing);

    if (job_user_id == 0) {
        return 1004; /* Built-in tasks cannot be deleted */
    }
    if (job_user_id != user_id) {
        return 1004; /* Cannot delete another user's task */
    }

    int rc = mf_scheduler_repo_job_delete(pool, user_id, job_id);
    if (rc != 0) {
        return -1;
    }

    scheduler_engine_notify_reload();
    return 0;
}

int
scheduler_usecase_job_toggle(
    void* pool, int64_t user_id, bool is_admin, int64_t job_id, bool enabled)
{
    if (!pool || job_id <= 0) {
        return -1;
    }

    csilk_json_t* existing = mf_scheduler_repo_job_get(pool, user_id, is_admin, job_id);
    if (!existing) {
        return 1004;
    }

    int64_t job_user_id = db_get_int(existing, "user_id");
    csilk_json_free(existing);

    if (job_user_id == 0 && !is_admin) {
        return 1004;
    }

    int rc = mf_scheduler_repo_job_set_enabled(pool, user_id, is_admin, job_id, enabled);
    if (rc != 0) {
        return -1;
    }

    scheduler_engine_notify_reload();
    return 0;
}

int
scheduler_usecase_job_run_now(void* pool, int64_t user_id, bool is_admin, int64_t job_id)
{
    if (!pool || job_id <= 0) {
        return -1;
    }

    csilk_json_t* existing = mf_scheduler_repo_job_get(pool, user_id, is_admin, job_id);
    if (!existing) {
        return 1004;
    }
    csilk_json_free(existing);

    return scheduler_engine_trigger_job_now(job_id, user_id, "manual_invoke");
}

int
scheduler_usecase_job_logs(void*          pool,
                           int64_t        user_id,
                           bool           is_admin,
                           int64_t        job_id,
                           int64_t        page,
                           int64_t        page_size,
                           csilk_json_t** out_resp)
{
    if (!pool || !out_resp) {
        return -1;
    }

    if (job_id > 0) {
        csilk_json_t* job = mf_scheduler_repo_job_get(pool, user_id, is_admin, job_id);
        if (!job) {
            return 1004;
        }
        csilk_json_free(job);
    }

    int64_t       total = 0;
    csilk_json_t* logs =
        mf_scheduler_repo_log_list(pool, user_id, is_admin, job_id, page, page_size, &total);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_array(resp, "items", logs ? logs : csilk_json_array());
    csilk_json_add_number(resp, "total", (double)total);
    csilk_json_add_number(resp, "page", (double)page);
    csilk_json_add_number(resp, "page_size", (double)page_size);

    *out_resp = resp;
    return 0;
}

int
scheduler_usecase_get_channel(void* pool, int64_t user_id, csilk_json_t** out_resp)
{
    if (!pool || !out_resp) {
        return -1;
    }

    csilk_json_t* ch = mf_scheduler_repo_get_user_channel(pool, user_id);
    if (!ch) {
        csilk_json_t* def = csilk_json_object();
        csilk_json_add_string(def, "channel_type", "none");
        csilk_json_add_string(def, "channel_name", "个人微信");
        csilk_json_add_string(def, "webhook_url", "");
        csilk_json_add_string(def, "secret_key_masked", "");
        csilk_json_add_number(def, "is_active", 0);
        *out_resp = def;
        return 0;
    }

    const char* sk = csilk_json_get_string(ch, "secret_key");
    char        masked[128] = "";
    if (sk && *sk) {
        size_t len = strlen(sk);
        if (len > 6) {
            snprintf(masked, sizeof(masked), "%.3s******%.3s", sk, sk + len - 3);
        } else {
            snprintf(masked, sizeof(masked), "******");
        }
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "channel_type", csilk_json_get_string(ch, "channel_type"));
    csilk_json_add_string(resp, "channel_name", csilk_json_get_string(ch, "channel_name"));
    csilk_json_add_string(resp, "webhook_url", csilk_json_get_string(ch, "webhook_url"));
    csilk_json_add_string(resp, "secret_key_masked", masked);
    csilk_json_add_number(resp, "is_active", (double)db_get_int(ch, "is_active"));

    csilk_json_free(ch);
    *out_resp = resp;
    return 0;
}

int
scheduler_usecase_save_channel(void* pool, int64_t user_id, const channel_save_dto_t* dto)
{
    if (!pool || !dto || !dto->channel_type[0]) {
        return -1;
    }

    const char* real_secret = dto->secret_key;
    char        saved_secret[128] = "";

    /* If secret_key is masked or unchanged, keep existing */
    if (dto->secret_key[0] == '*' || strstr(dto->secret_key, "******")) {
        csilk_json_t* existing = mf_scheduler_repo_get_user_channel(pool, user_id);
        if (existing) {
            const char* orig = csilk_json_get_string(existing, "secret_key");
            if (orig) {
                snprintf(saved_secret, sizeof(saved_secret), "%s", orig);
                real_secret = saved_secret;
            }
            csilk_json_free(existing);
        }
    }

    return mf_scheduler_repo_save_user_channel(
        pool, user_id, dto->channel_type, dto->channel_name, dto->webhook_url, real_secret);
}

int
scheduler_usecase_test_channel(void* pool, int64_t user_id, char* err_buf, size_t err_len)
{
    if (!pool) {
        return -1;
    }

    csilk_json_t* ch = mf_scheduler_repo_get_user_channel(pool, user_id);
    if (!ch || db_get_int(ch, "is_active") == 0) {
        if (ch) {
            csilk_json_free(ch);
        }
        if (err_buf && err_len > 0) {
            snprintf(err_buf, err_len, "当前用户尚未配置或启用微信推送通道");
        }
        return -1;
    }

    push_channel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    const char* ct = csilk_json_get_string(ch, "channel_type");
    const char* cn = csilk_json_get_string(ch, "channel_name");
    const char* wu = csilk_json_get_string(ch, "webhook_url");
    const char* sk = csilk_json_get_string(ch, "secret_key");

    if (ct) {
        snprintf(cfg.channel_type, sizeof(cfg.channel_type), "%s", ct);
    }
    if (cn) {
        snprintf(cfg.channel_name, sizeof(cfg.channel_name), "%s", cn);
    }
    if (wu) {
        snprintf(cfg.webhook_url, sizeof(cfg.webhook_url), "%s", wu);
    }
    if (sk) {
        snprintf(cfg.secret_key, sizeof(cfg.secret_key), "%s", sk);
    }
    csilk_json_free(ch);

    const char* test_msg = "### 🎉 Minefolio 微信推送连通性测试成功\n\n"
                           "> 恭喜！您已成功打通个人微信通知服务通道。\n\n"
                           "- **接入通道**: 专属微信提醒\n"
                           "- **触发状态**: 握手验证通过\n"
                           "- **服务效用**: 今后定时简报、异动预警将第一时间推送到您的微信！\n\n"
                           "---\n*Minefolio 自动化调度中心*";

    return wechat_push_service_send(&cfg, "Minefolio 推送连通性测试", test_msg, err_buf, err_len);
}
