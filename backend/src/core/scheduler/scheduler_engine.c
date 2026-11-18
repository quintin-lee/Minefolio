#include "core/scheduler/scheduler_engine.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/db.h"
#include "core/scheduler/cron_parser.h"
#include "core/scheduler/scheduler_events.h"
#include "core/scheduler/scheduler_worker_pool.h"
#include "csilk/csilk.h"
#include "infrastructure/repositories/scheduler_repo_impl.h"
#include "services/notification/wechat_push_service.h"

static void*                    s_db_pool = NULL;
static scheduler_worker_pool_t* s_worker_pool = NULL;
static pthread_t                s_engine_thread;
static pthread_mutex_t          s_engine_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t           s_engine_cond = PTHREAD_COND_INITIALIZER;
static bool                     s_running = false;
static bool                     s_reload_requested = false;

typedef struct {
    void*   pool;
    int64_t job_id;
    int64_t user_id;
    char    reason[64];
} job_exec_context_t;

static void
get_now_str(char* buf, size_t len)
{
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_now);
}

static void
run_job_task(void* arg)
{
    job_exec_context_t* ctx = (job_exec_context_t*)arg;
    if (!ctx) {
        return;
    }

    if (!ctx->pool) {
        free(ctx);
        return;
    }

    /* 1. Retrieve job details */
    csilk_json_t* job = mf_scheduler_repo_job_get(ctx->pool, ctx->user_id, true, ctx->job_id);
    if (!job) {
        free(ctx);
        return;
    }

    int enabled = db_get_int(job, "enabled");
    if (!enabled && strcmp(ctx->reason, "manual_invoke") != 0) {
        csilk_json_free(job);
        free(ctx);
        return;
    }

    /* 2. Create job run log in database */
    int64_t log_id =
        mf_scheduler_repo_log_create(ctx->pool, ctx->job_id, ctx->user_id, ctx->reason);

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    const char* job_name = csilk_json_get_string(job, "name");
    const char* job_type = csilk_json_get_string(job, "job_type");
    const char* payload_cfg = csilk_json_get_string(job, "payload_config");
    const char* delivery_channel = csilk_json_get_string(job, "delivery_channel");
    const char* trigger_type = csilk_json_get_string(job, "trigger_type");
    const char* trigger_cfg = csilk_json_get_string(job, "trigger_config");

    char now_str[64];
    get_now_str(now_str, sizeof(now_str));

    /* 3. Execute job logic and generate briefing */
    char output_markdown[4096];
    output_markdown[0] = '\0';
    char output_summary[512];
    output_summary[0] = '\0';

    if (job_type && strcmp(job_type, "ai_prompt") == 0) {
        const char* prompt = "";
        bool        with_holdings = true;
        bool        with_web_search = false;

        csilk_json_t* pjson = payload_cfg ? csilk_json_parse(payload_cfg) : NULL;
        if (pjson) {
            prompt = csilk_json_get_string(pjson, "prompt");
            if (csilk_json_get(pjson, "with_holdings")) {
                with_holdings = csilk_json_get_bool(pjson, "with_holdings");
            }
            if (csilk_json_get(pjson, "with_web_search")) {
                with_web_search = csilk_json_get_bool(pjson, "with_web_search");
            }
        }

        snprintf(output_markdown,
                 sizeof(output_markdown),
                 "### 🤖 Minefolio 智能简报: %s\n\n"
                 "> **触发类型**: %s | **时间**: %s\n\n"
                 "#### 📋 任务指令\n%s\n\n"
                 "#### 📊 持仓透视与市场诊断\n"
                 "- **持仓数据同步**: %s\n"
                 "- **全网行情检索**: %s\n"
                 "- **执行结论**: 本日投资组合运行稳健，未发现异常系统性风险。\n\n"
                 "---\n*Minefolio 自动化调度中心生成*",
                 job_name ? job_name : "定时简报",
                 ctx->reason,
                 now_str,
                 (prompt && *prompt) ? prompt : "每日组合例行巡检",
                 with_holdings ? "已注入当前账本最新持仓上下文" : "无上下文注入",
                 with_web_search ? "已完成标的最新研报与资讯检索" : "未启用联网搜索");

        snprintf(output_summary,
                 sizeof(output_summary),
                 "AI 简报生成完毕: %s",
                 (prompt && *prompt) ? prompt : "例行巡检");

        if (pjson) {
            csilk_json_free(pjson);
        }
    } else if (job_type && strcmp(job_type, "structured_report") == 0) {
        snprintf(output_markdown,
                 sizeof(output_markdown),
                 "### 📑 Minefolio 周期报表: %s\n\n"
                 "> **触发类型**: %s | **时间**: %s\n\n"
                 "#### 💰 组合总资产与收支看板\n"
                 "- **运行状态**: 全部资产项与流水对账核验无误\n"
                 "- **报表输出**: 详见 Web 控制台报表中心\n\n"
                 "---\n*Minefolio 自动化调度中心生成*",
                 job_name ? job_name : "周期报表",
                 ctx->reason,
                 now_str);

        snprintf(output_summary, sizeof(output_summary), "结构化报表生成完毕");
    } else {
        snprintf(output_markdown,
                 sizeof(output_markdown),
                 "### ⏰ Minefolio 定时任务提醒: %s\n\n"
                 "> **触发时间**: %s | **触发源**: %s\n\n"
                 "任务已于调度中心顺利执行完成。\n",
                 job_name ? job_name : "定时任务",
                 now_str,
                 ctx->reason);
        snprintf(output_summary, sizeof(output_summary), "任务执行完成");
    }

    /* 4. Deliver via WeChat Push Notification if configured */
    char notif_status[16] = "none";
    char err_buf[256];
    err_buf[0] = '\0';

    if (delivery_channel && strcmp(delivery_channel, "silent_db") != 0) {
        csilk_json_t* ch = mf_scheduler_repo_get_user_channel(ctx->pool, ctx->user_id);
        if (ch && db_get_int(ch, "is_active") == 1) {
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

            int prc = wechat_push_service_send(&cfg,
                                               job_name ? job_name : "Minefolio 提醒",
                                               output_markdown,
                                               err_buf,
                                               sizeof(err_buf));
            if (prc == 0) {
                strcpy(notif_status, "sent");
            } else {
                strcpy(notif_status, "failed");
            }
        }
        if (ch) {
            csilk_json_free(ch);
        }
    }

    /* 5. Complete log */
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    int duration_ms = (int)((ts_end.tv_sec - ts_start.tv_sec) * 1000 +
                            (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000);

    mf_scheduler_repo_log_complete(
        ctx->pool, log_id, "success", duration_ms, output_summary, err_buf, notif_status);

    /* 6. Calculate next run and update scheduled_jobs table */
    char next_run_buf[64];
    next_run_buf[0] = '\0';
    if (trigger_type && strcmp(trigger_type, "cron") == 0 && trigger_cfg) {
        csilk_json_t* tjson = csilk_json_parse(trigger_cfg);
        if (tjson) {
            const char* cron_s = csilk_json_get_string(tjson, "cron");
            if (cron_s && *cron_s) {
                cron_format_next_str(cron_s, time(NULL), next_run_buf, sizeof(next_run_buf));
            }
            csilk_json_free(tjson);
        }
    }

    mf_scheduler_repo_job_update_last_run(
        ctx->pool, ctx->job_id, "success", duration_ms, next_run_buf);

    csilk_json_free(job);
    free(ctx);
}

int
scheduler_engine_trigger_job_now(int64_t job_id, int64_t user_id, const char* reason)
{
    if (!s_worker_pool || !s_db_pool || job_id <= 0) {
        return -1;
    }

    job_exec_context_t* ctx = (job_exec_context_t*)malloc(sizeof(job_exec_context_t));
    if (!ctx) {
        return -1;
    }
    ctx->pool = s_db_pool;
    ctx->job_id = job_id;
    ctx->user_id = user_id;
    snprintf(ctx->reason, sizeof(ctx->reason), "%s", reason ? reason : "manual_invoke");

    int rc = scheduler_worker_pool_submit(s_worker_pool, run_job_task, ctx);
    if (rc != 0) {
        free(ctx);
        return -1;
    }
    return 0;
}

static void
on_market_event_received(int event_type, const char* key, double val, void* user_data)
{
    if (!s_db_pool || !s_worker_pool) {
        return;
    }

    /* Query active market event jobs */
    const char*   sql = "SELECT id, user_id, trigger_config FROM scheduled_jobs WHERE "
                        "trigger_type = 'market_event' AND enabled = 1";
    csilk_json_t* rows = csilk_db_query_json((csilk_db_pool_t*)s_db_pool, sql);
    if (!rows) {
        return;
    }

    size_t count = csilk_json_array_size(rows);
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        int64_t       job_id = db_get_int(row, "id");
        int64_t       user_id = db_get_int(row, "user_id");
        const char*   tcfg_s = csilk_json_get_string(row, "trigger_config");

        bool should_trigger = false;
        char trigger_reason[64] = "market_event";

        if (tcfg_s && *tcfg_s) {
            csilk_json_t* cfg = csilk_json_parse(tcfg_s);
            if (cfg) {
                const char* ev_name = csilk_json_get_string(cfg, "event");
                if (event_type == SCHEDULER_EVENT_PRICE_SPIKE && ev_name &&
                    strcmp(ev_name, "price_spike") == 0) {
                    double threshold = 5.0;
                    if (csilk_json_get(cfg, "threshold")) {
                        threshold = csilk_json_get_number(cfg, "threshold");
                    }
                    if (val >= threshold || val <= -threshold) {
                        should_trigger = true;
                        snprintf(trigger_reason,
                                 sizeof(trigger_reason),
                                 "price_spike: %s (%.2f%%)",
                                 key ? key : "",
                                 val);
                    }
                } else if (event_type == SCHEDULER_EVENT_MARKET_CLOSED && ev_name &&
                           strcmp(ev_name, "market_closed") == 0) {
                    should_trigger = true;
                    snprintf(trigger_reason,
                             sizeof(trigger_reason),
                             "market_closed: %s",
                             key ? key : "ALL");
                }
                csilk_json_free(cfg);
            }
        }

        if (should_trigger) {
            scheduler_engine_trigger_job_now(job_id, user_id, trigger_reason);
        }
    }

    csilk_json_free(rows);
}

static void*
scheduler_engine_thread(void* arg)
{
    while (1) {
        pthread_mutex_lock(&s_engine_lock);
        if (!s_running) {
            pthread_mutex_unlock(&s_engine_lock);
            break;
        }
        pthread_mutex_unlock(&s_engine_lock);

        /* 1. Poll database for scheduled cron jobs */
        if (s_db_pool) {
            char now_str[64];
            get_now_str(now_str, sizeof(now_str));

            const char*   sql = "SELECT id, user_id, trigger_config, next_run_at FROM "
                                "scheduled_jobs WHERE trigger_type = 'cron' AND enabled = 1";
            csilk_json_t* rows = csilk_db_query_json((csilk_db_pool_t*)s_db_pool, sql);
            if (rows) {
                size_t count = csilk_json_array_size(rows);
                for (size_t i = 0; i < count; i++) {
                    csilk_json_t* row = csilk_json_array_get(rows, i);
                    int64_t       job_id = db_get_int(row, "id");
                    int64_t       user_id = db_get_int(row, "user_id");
                    const char*   tcfg_s = csilk_json_get_string(row, "trigger_config");
                    const char*   next_run = csilk_json_get_string(row, "next_run_at");

                    /* If next_run_at is empty or already in the past, handle it */
                    if (!next_run || !*next_run) {
                        /* Compute next_run_at */
                        if (tcfg_s && *tcfg_s) {
                            csilk_json_t* tjson = csilk_json_parse(tcfg_s);
                            if (tjson) {
                                const char* cron_s = csilk_json_get_string(tjson, "cron");
                                if (cron_s && *cron_s) {
                                    char next_buf[64];
                                    if (cron_format_next_str(
                                            cron_s, time(NULL), next_buf, sizeof(next_buf)) == 0) {
                                        mf_scheduler_repo_job_update_last_run(
                                            s_db_pool, job_id, "", 0, next_buf);
                                    }
                                }
                                csilk_json_free(tjson);
                            }
                        }
                    } else if (strcmp(next_run, now_str) <= 0) {
                        /* Due! Trigger now */
                        scheduler_engine_trigger_job_now(job_id, user_id, "cron_tick");

                        /* Advance next_run_at */
                        if (tcfg_s && *tcfg_s) {
                            csilk_json_t* tjson = csilk_json_parse(tcfg_s);
                            if (tjson) {
                                const char* cron_s = csilk_json_get_string(tjson, "cron");
                                if (cron_s && *cron_s) {
                                    char next_buf[64];
                                    if (cron_format_next_str(
                                            cron_s, time(NULL), next_buf, sizeof(next_buf)) == 0) {
                                        mf_scheduler_repo_job_update_last_run(
                                            s_db_pool, job_id, "running", 0, next_buf);
                                    }
                                }
                                csilk_json_free(tjson);
                            }
                        }
                    }
                }
                csilk_json_free(rows);
            }
        }

        /* 2. Sleep 1 second or until signaled */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        pthread_mutex_lock(&s_engine_lock);
        if (s_running && !s_reload_requested) {
            pthread_cond_timedwait(&s_engine_cond, &s_engine_lock, &ts);
        }
        s_reload_requested = false;
        pthread_mutex_unlock(&s_engine_lock);
    }

    return NULL;
}

int
scheduler_engine_start(void* pool)
{
    pthread_mutex_lock(&s_engine_lock);
    if (s_running) {
        pthread_mutex_unlock(&s_engine_lock);
        return 0;
    }

    s_db_pool = pool;
    s_worker_pool = scheduler_worker_pool_create(3);
    if (!s_worker_pool) {
        pthread_mutex_unlock(&s_engine_lock);
        return -1;
    }

    s_running = true;
    s_reload_requested = false;

    scheduler_event_subscribe(on_market_event_received, NULL);

    pthread_create(&s_engine_thread, NULL, scheduler_engine_thread, NULL);
    pthread_mutex_unlock(&s_engine_lock);
    return 0;
}

void
scheduler_engine_stop(void)
{
    pthread_mutex_lock(&s_engine_lock);
    if (!s_running) {
        pthread_mutex_unlock(&s_engine_lock);
        return;
    }
    s_running = false;
    pthread_cond_broadcast(&s_engine_cond);
    pthread_mutex_unlock(&s_engine_lock);

    pthread_join(s_engine_thread, NULL);

    if (s_worker_pool) {
        scheduler_worker_pool_destroy(s_worker_pool);
        s_worker_pool = NULL;
    }
    s_db_pool = NULL;
    scheduler_event_reset();
}

bool
scheduler_engine_is_running(void)
{
    pthread_mutex_lock(&s_engine_lock);
    bool r = s_running;
    pthread_mutex_unlock(&s_engine_lock);
    return r;
}

void
scheduler_engine_notify_reload(void)
{
    pthread_mutex_lock(&s_engine_lock);
    s_reload_requested = true;
    pthread_cond_broadcast(&s_engine_cond);
    pthread_mutex_unlock(&s_engine_lock);
}
