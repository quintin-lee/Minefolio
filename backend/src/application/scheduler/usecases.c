#include "application/scheduler/usecases.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "application/ai/im_push_usecases.h"
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
    char today_start[64], now_str[64];
    get_today_start_str(today_start, sizeof(today_start));
    get_now_str(now_str, sizeof(now_str));

    /* 1. Active jobs count */
    int64_t active_jobs = scheduler_repo_count_active(pool, user_id);

    /* 2. Today's run count */
    int64_t today_runs = scheduler_repo_count_runs_today(pool, user_id, today_start);

    /* 3. Nearest next run job */
    char next_job_name[128] = "";
    char next_job_time[64] = "";
    scheduler_repo_get_next_run(pool,
                                user_id,
                                now_str,
                                next_job_name,
                                sizeof(next_job_name),
                                next_job_time,
                                sizeof(next_job_time));

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
    csilk_json_t* ilink_st = im_push_usecase_build_ilink_status(pool, user_id);
    if (ilink_st) {
        csilk_json_add_object(ch_obj, "ilink_status", ilink_st);
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

    int     is_builtin = db_get_int(existing, "is_builtin");
    int64_t job_user_id = db_get_int(existing, "user_id");
    csilk_json_free(existing);

    if (is_builtin || job_user_id == 0) {
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

    int     is_builtin = db_get_int(existing, "is_builtin");
    int64_t job_user_id = db_get_int(existing, "user_id");
    csilk_json_free(existing);

    /* Built-in global tasks and non-owned tasks require admin privileges to toggle */
    if ((is_builtin || job_user_id == 0 || job_user_id != user_id) && !is_admin) {
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
        csilk_json_t* ilink_st = im_push_usecase_build_ilink_status(pool, user_id);
        if (ilink_st) {
            csilk_json_add_object(def, "ilink_status", ilink_st);
        }
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
    csilk_json_t* ilink_st = im_push_usecase_build_ilink_status(pool, user_id);
    if (ilink_st) {
        csilk_json_add_object(resp, "ilink_status", ilink_st);
    }

    csilk_json_free(ch);
    *out_resp = resp;
    return 0;
}

static bool
scheduler_channel_type_allowed(const char* ct)
{
    if (!ct || !ct[0]) {
        return false;
    }
    return strcasecmp(ct, "none") == 0 || strcasecmp(ct, "serverchan") == 0 ||
           strcasecmp(ct, "pushdeer") == 0 || strcasecmp(ct, "wechat_work") == 0 ||
           strcasecmp(ct, "wechatwork") == 0 || strcasecmp(ct, "qywx") == 0 ||
           strcasecmp(ct, "webhook") == 0 || strcasecmp(ct, "wechat_ilink") == 0;
}

int
scheduler_usecase_save_channel(void* pool, int64_t user_id, const channel_save_dto_t* dto)
{
    if (!pool || !dto) {
        return -1;
    }
    if (!scheduler_channel_type_allowed(dto->channel_type)) {
        return 1002;
    }

    if (strcasecmp(dto->channel_type, "wechat_ilink") == 0 ||
        strcasecmp(dto->channel_type, "none") == 0) {
        const char* norm =
            strcasecmp(dto->channel_type, "wechat_ilink") == 0 ? "wechat_ilink" : "none";
        return mf_scheduler_repo_save_user_channel(pool, user_id, norm, dto->channel_name, "", "");
    }

    const char* real_secret = dto->secret_key;
    char        saved_secret[128] = "";

    /* If secret_key is empty, masked or unchanged, keep existing */
    if (dto->secret_key[0] == '\0' || dto->secret_key[0] == '*' ||
        strstr(dto->secret_key, "******")) {
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

static bool
check_ilink_test_push_status(
    void* pool, int64_t push_id, char* err_buf, size_t err_len, int* out_done_rc)
{
    csilk_json_t* pr = im_push_usecase_get(pool, push_id);
    if (!pr) {
        return false;
    }
    const char* st = csilk_json_get_string(pr, "status");
    const char* le = csilk_json_get_string(pr, "last_error");
    if (st && strcmp(st, "sent") == 0) {
        csilk_json_free(pr);
        *out_done_rc = 0;
        return true;
    }
    if (st && (strcmp(st, "failed") == 0 || strcmp(st, "expired") == 0)) {
        if (err_buf && err_len > 0) {
            snprintf(err_buf, err_len, "%s", (le && le[0]) ? le : "微信 iLink 推送测试失败");
        }
        csilk_json_free(pr);
        *out_done_rc = -1;
        return true;
    }
    csilk_json_free(pr);
    return false;
}

int
scheduler_usecase_test_channel(void* pool, int64_t user_id, char* err_buf, size_t err_len)
{
    if (err_buf && err_len > 0) {
        err_buf[0] = '\0';
    }
    if (!pool) {
        return -1;
    }

    csilk_json_t* ch = mf_scheduler_repo_get_user_channel(pool, user_id);
    const char*   ct_chk = ch ? csilk_json_get_string(ch, "channel_type") : NULL;
    if (!ch || db_get_int(ch, "is_active") == 0 || !ct_chk || !ct_chk[0] ||
        strcasecmp(ct_chk, "none") == 0) {
        if (ch) {
            csilk_json_free(ch);
        }
        if (err_buf && err_len > 0) {
            snprintf(err_buf, err_len, "当前用户尚未配置或启用微信推送通道");
        }
        return -1;
    }

    const char* test_msg = "### 🎉 Minefolio 微信推送连通性测试成功\n\n"
                           "> 恭喜！您已成功打通个人微信通知服务通道。\n\n"
                           "- **接入通道**: 专属微信提醒\n"
                           "- **触发状态**: 握手验证通过\n"
                           "- **服务效用**: 今后定时简报、异动预警将第一时间推送到您的微信！\n\n"
                           "---\n*Minefolio 自动化调度中心*";

    const char* ct = csilk_json_get_string(ch, "channel_type");
    if (ct && strcasecmp(ct, "wechat_ilink") == 0) {
        csilk_json_free(ch);
        int64_t push_id = 0;
        if (im_push_usecase_enqueue(pool,
                                    user_id,
                                    0,
                                    "test",
                                    "Minefolio 推送连通性测试",
                                    test_msg,
                                    30,
                                    &push_id,
                                    err_buf,
                                    err_len) != 0) {
            return -1;
        }
        int         timeout_ms = 10000;
        const char* env_to = getenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS");
        if (env_to && env_to[0]) {
            char* endp = NULL;
            long  parsed = strtol(env_to, &endp, 10);
            if (endp != env_to && *endp == '\0' && parsed > 0) {
                if (parsed < 100) {
                    parsed = 100;
                } else if (parsed > 60000) {
                    parsed = 60000;
                }
                timeout_ms = (int)parsed;
            }
        }
        int waited_ms = 0;
        int done_rc = -1;
        while (waited_ms < timeout_ms) {
            if (check_ilink_test_push_status(pool, push_id, err_buf, err_len, &done_rc)) {
                return done_rc;
            }
            struct timespec req = {0, 100000000L}; /* 100ms */
            nanosleep(&req, NULL);
            waited_ms += 100;
        }
        if (check_ilink_test_push_status(pool, push_id, err_buf, err_len, &done_rc)) {
            return done_rc;
        }
        (void)im_push_usecase_cancel_expired_test(pool, push_id, "iLink 网关未在测试窗口内响应");
        if (err_buf && err_len > 0) {
            snprintf(err_buf,
                     err_len,
                     "iLink 网关未在 %d 秒内响应，请检查 ilink-server 进程是否运行且网络可达",
                     timeout_ms / 1000 > 0 ? timeout_ms / 1000 : 1);
        }
        return -1;
    }

    push_channel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
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

    return wechat_push_service_send(&cfg, "Minefolio 推送连通性测试", test_msg, err_buf, err_len);
}

typedef struct {
    const char* name;
    const char* desc;
    const char* cron_expr;
    const char* handler;
    const char* delivery_channel;
} builtin_job_seed_t;

int
scheduler_usecase_seed_builtin_jobs(void* pool)
{
    if (!pool) {
        return -1;
    }

    static const builtin_job_seed_t seeds[] = {
        {"全市场行情与基金净值同步",
         "定期自动抓取并更新用户自选与持仓标的的最新股票/ETF/基金行情与净值",           "*/15 9-15 * * 1-5",
         "market_quote_sync",   "silent_db"   },
        {"全球大盘指数轮询",
         "同步上证、深证、恒生、标普500、纳斯达克等全球基准股指最新点位与涨跌幅",       "*/5 * * * *",
         "global_indices_sync", "silent_db"   },
        {"实时外汇基准汇率同步",
         "定时同步央行及国际外汇市场基准中行折算价与离岸外汇汇率（CNY/USD/HKD/EUR等）", "0 */1 * * 1-5",
         "forex_rates_sync",    "silent_db"   },
        {"定投划转计划每日巡检",
         "每日0点巡检有效定投计划，在执行日自动生成待确认扣款与买入流水",               "0 0 * * *",
         "dca_plan_trigger",    "silent_db"   },
        {"AI 每日金融快讯提炼",
         "每日早间自动归集财经大盘要闻，通过 AI 提炼核心资讯与市场情绪研报",            "0 8 * * *",
         "ai_news_digest",      "user_default"},
        {"MCP 外部进程池空闲回收",
         "定期巡检并清理空闲超过存活周期的 Model Context Protocol (MCP) 外部子进程",    "*/5 * * * *",
         "mcp_pool_cleanup",    "silent_db"   },
        {"系统数据库每日自动备份",
         "每日凌晨3点执行 SQLite 在线热备份并自动轮转保留最近7天备份副本",              "0 3 * * *",
         "db_auto_backup",      "silent_db"   },
    };

    size_t count = sizeof(seeds) / sizeof(seeds[0]);
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* existing = mf_scheduler_repo_job_get_by_name(pool, seeds[i].name);
        if (existing) {
            csilk_json_free(existing);
            continue;
        }

        char trig_cfg[128];
        snprintf(trig_cfg, sizeof(trig_cfg), "{\"cron\":\"%s\"}", seeds[i].cron_expr);

        char pay_cfg[128];
        snprintf(pay_cfg, sizeof(pay_cfg), "{\"handler\":\"%s\"}", seeds[i].handler);

        char next_run[64] = "";
        cron_format_next_str(seeds[i].cron_expr, time(NULL), next_run, sizeof(next_run));

        mf_scheduler_repo_job_create_builtin(pool,
                                             seeds[i].name,
                                             seeds[i].desc,
                                             "system_builtin",
                                             "cron",
                                             trig_cfg,
                                             pay_cfg,
                                             seeds[i].delivery_channel,
                                             "{}",
                                             next_run);
    }

    return 0;
}
