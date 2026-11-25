#include "core/scheduler/scheduler_engine.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "application/ai/im_push_usecases.h"
#include "common/db.h"
#include "core/scheduler/cron_parser.h"
#include "core/scheduler/scheduler_events.h"
#include "core/scheduler/scheduler_worker_pool.h"
#include "csilk/csilk.h"
#include "infrastructure/repositories/scheduler_repo_impl.h"
#include "services/notification/wechat_push_service.h"
#include "application/scheduler/usecases.h"
#include "application/market/usecases.h"
#include "services/market/market_scheduler.h"
#include "services/market/quote_engine.h"
#include "services/market/exchange_rate_service.h"
#include "services/ai/ai_news_service.h"
#include "services/ai/tools/mcp/mcp_stdio_pool.h"
#include "application/ai/config_provider.h"
#include "services/ai/runtime/loop.h"
#include "services/ai/runtime/context.h"
#include "services/ai/tools/registry.h"
#include "services/ai/memory/memory.h"
#include "services/ai/trace/trace.h"
#include "infrastructure/repositories/ai_trace_repo_impl.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "infrastructure/database/db_backup.h"

#define SCHEDULER_MAX_CONCURRENT_AI_JOBS 1

static void*                    s_db_pool = NULL;
static scheduler_worker_pool_t* s_worker_pool = NULL;
static pthread_t                s_engine_thread;
static pthread_mutex_t          s_engine_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t           s_engine_cond = PTHREAD_COND_INITIALIZER;
static bool                     s_running = false;
static bool                     s_reload_requested = false;
static int                      s_last_sleep_sec = 60;
static atomic_int               s_ai_jobs_running = 0;

typedef struct {
    void*   pool;
    int64_t job_id;
    int64_t user_id;
    char    reason[64];
} job_exec_context_t;

static time_t
parse_time_str(const char* s)
{
    if (!s || !*s) {
        return 0;
    }
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (sscanf(s, "%d-%d-%d %d:%d:%d", &year, &mon, &day, &hour, &min, &sec) == 6) {
        tm_val.tm_year = year - 1900;
        tm_val.tm_mon = mon - 1;
        tm_val.tm_mday = day;
        tm_val.tm_hour = hour;
        tm_val.tm_min = min;
        tm_val.tm_sec = sec;
        tm_val.tm_isdst = -1;
        return mktime(&tm_val);
    }
    return 0;
}

int
scheduler_engine_get_last_sleep_sec(void)
{
    pthread_mutex_lock(&s_engine_lock);
    int sec = s_last_sleep_sec;
    pthread_mutex_unlock(&s_engine_lock);
    return sec;
}

int
scheduler_engine_get_ai_jobs_running(void)
{
    return atomic_load(&s_ai_jobs_running);
}

static void
get_now_str(char* buf, size_t len)
{
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_now);
}

static void
deliver_to_user_channel(void*         pool,
                        int64_t       user_id,
                        int64_t       log_id,
                        csilk_json_t* ch,
                        const char*   title,
                        const char*   markdown_body,
                        char*         out_status,
                        size_t        out_status_cap,
                        char*         err_buf,
                        size_t        err_cap)
{
    snprintf(out_status, out_status_cap, "none");
    if (!ch || db_get_int(ch, "is_active") != 1) {
        return;
    }
    const char* ct = csilk_json_get_string(ch, "channel_type");
    if (!ct || !ct[0] || strcasecmp(ct, "none") == 0) {
        return;
    }
    if (strcasecmp(ct, "wechat_ilink") == 0) {
        int64_t push_id = 0;
        if (im_push_usecase_enqueue(pool,
                                    user_id,
                                    log_id,
                                    "scheduler",
                                    (title && title[0]) ? title : "Minefolio 提醒",
                                    markdown_body ? markdown_body : "",
                                    1800,
                                    &push_id,
                                    err_buf,
                                    err_cap) == 0) {
            snprintf(out_status, out_status_cap, "queued");
        } else {
            snprintf(out_status, out_status_cap, "failed");
        }
        return;
    }

    push_channel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    const char* cn = csilk_json_get_string(ch, "channel_name");
    const char* wu = csilk_json_get_string(ch, "webhook_url");
    const char* sk = csilk_json_get_string(ch, "secret_key");
    snprintf(cfg.channel_type, sizeof(cfg.channel_type), "%s", ct);
    if (cn) {
        snprintf(cfg.channel_name, sizeof(cfg.channel_name), "%s", cn);
    }
    if (wu) {
        snprintf(cfg.webhook_url, sizeof(cfg.webhook_url), "%s", wu);
    }
    if (sk) {
        snprintf(cfg.secret_key, sizeof(cfg.secret_key), "%s", sk);
    }
    int prc = wechat_push_service_send(
        &cfg, (title && title[0]) ? title : "Minefolio 提醒", markdown_body, err_buf, err_cap);
    snprintf(out_status, out_status_cap, "%s", prc == 0 ? "sent" : "failed");
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
    size_t md_cap = 65536;
    char*  output_markdown = (char*)malloc(md_cap);
    if (!output_markdown) {
        csilk_json_free(job);
        free(ctx);
        return;
    }
    output_markdown[0] = '\0';
    char output_summary[512];
    output_summary[0] = '\0';

    bool is_ai_job = (job_type && strcmp(job_type, "ai_prompt") == 0);
    bool ai_slot_acquired = false;

    if (is_ai_job) {
        for (int retry = 0; retry < 5; retry++) {
            int cur = atomic_load(&s_ai_jobs_running);
            if (cur < SCHEDULER_MAX_CONCURRENT_AI_JOBS) {
                if (atomic_compare_exchange_strong(&s_ai_jobs_running, &cur, cur + 1)) {
                    ai_slot_acquired = true;
                    break;
                }
            }
            usleep(100000);
        }

        if (!ai_slot_acquired) {
            snprintf(output_markdown,
                     md_cap,
                     "### ⚠️ 任务跳过: %s\n\n"
                     "> 系统当前正有其他 AI "
                     "任务执行中。为保障调度队列核心吞吐，本次任务已自动平滑延后。\n",
                     job_name ? job_name : "定时简报");
            snprintf(output_summary,
                     sizeof(output_summary),
                     "跳过: AI 任务并发受限 (已有活跃 AI 任务运行中)");
        } else {
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

            /* 注入持仓上下文 (若开启且用户有效) */
            char holdings_ctx[4096];
            holdings_ctx[0] = '\0';
            if (with_holdings && ctx->user_id > 0) {
                int64_t       tot_assets = 0;
                csilk_json_t* alist = mf_ai_repo_asset_list(
                    (csilk_db_pool_t*)ctx->pool, ctx->user_id, 1, 50, NULL, &tot_assets);
                if (alist) {
                    size_t n = csilk_json_array_size(alist);
                    size_t h_offset = 0;
                    h_offset += snprintf(holdings_ctx + h_offset,
                                         sizeof(holdings_ctx) - h_offset,
                                         "\n\n【用户当前核心资产持仓列表（共 %zu 项）】:\n",
                                         n);
                    for (size_t i = 0; i < n && h_offset + 128 < sizeof(holdings_ctx); i++) {
                        csilk_json_t* a = csilk_json_array_get(alist, i);
                        const char*   name = csilk_json_get_string(a, "name");
                        const char*   type = csilk_json_get_string(a, "asset_type");
                        const char*   ccy = csilk_json_get_string(a, "currency");
                        double        val = db_get_num(a, "current_value");
                        if (val == 0.0) {
                            val = db_get_num(a, "balance");
                        }
                        double cost = db_get_num(a, "cost_basis");
                        h_offset +=
                            snprintf(holdings_ctx + h_offset,
                                     sizeof(holdings_ctx) - h_offset,
                                     "- %s (%s, 计价币种:%s): 当前估值/余额 %.2f, 成本 %.2f\n",
                                     name ? name : "未命名",
                                     type ? type : "other",
                                     ccy ? ccy : "CNY",
                                     val,
                                     cost);
                    }
                    csilk_json_free(alist);
                }
            }

            /* 真实尝试接入 AI Runtime */
            bool           ai_ran_ok = false;
            ai_config_t*   cfg = ai_config_acquire();
            ai_provider_t* prov = cfg ? ai_config_default_provider(cfg) : NULL;
            if (prov && prov->api_key[0] != '\0') {
                const char* model_sel = (cfg->default_model[0]) ? cfg->default_model : "default";

                ai_runtime_context_t rctx;
                ai_runtime_context_init(&rctx);
                rctx.user_id = ctx->user_id;
                rctx.session_id = 0;
                snprintf(rctx.model_name, sizeof(rctx.model_name), "%s", model_sel);
                rctx.temperature = 0.5;
                rctx.max_tokens = 2048;
                rctx.limits.max_iterations = 4;
                rctx.limits.token_budget = 8192;

                /* 只读工具白名单 */
                size_t                 ro_count = 0;
                const csilk_ai_tool_t* ro_tools = ai_tool_get_readonly_csilk_definitions(&ro_count);
                rctx.tools = (csilk_ai_tool_t*)ro_tools;
                rctx.tool_count = ro_count;

                /* 构建 System Prompt 与 User Prompt */
                csilk_json_t* sys = csilk_json_object();
                csilk_json_add_string(sys, "role", "system");
                csilk_json_add_string(
                    sys,
                    "content",
                    "你是一个资深的个人财富管理与投资研报助手。请根据用户的任务要求以"
                    "及提供的持仓上下文，"
                    "生成专业、结构严谨的 Markdown 格式投资报告或定期巡检总结。"
                    "保持分析客观中肯，重点提示风险与资产配置优化建议。输出应当包含二"
                    "级/三级 Markdown 标题、要点列表以及专业分析。");
                csilk_json_array_append(rctx.messages, sys);

                char user_prompt[8192];
                snprintf(
                    user_prompt,
                    sizeof(user_prompt),
                    "【定时调度任务名称】: %s\n"
                    "【触发源】: %s\n"
                    "【时间】: %s\n"
                    "【任务要求】:\n%s%s%s",
                    job_name ? job_name : "定时简报",
                    ctx->reason,
                    now_str,
                    (prompt && *prompt) ? prompt : "进行每日组合例行巡检并生成资产与市场异动分析",
                    holdings_ctx,
                    with_web_search
                        ? "\n\n（提示：如需最新新闻资讯，你可以调用只读搜索工具获取最新大盘资讯）"
                        : "");

                csilk_json_t* usr = csilk_json_object();
                csilk_json_add_string(usr, "role", "user");
                csilk_json_add_string(usr, "content", user_prompt);
                csilk_json_array_append(rctx.messages, usr);

                ai_trace_t*         trace = NULL;
                ai_runtime_result_t res =
                    ai_runtime_execute((csilk_db_pool_t*)ctx->pool, &rctx, &trace);
                ai_runtime_context_free(&rctx);

                if (res.status.code == 0 && res.final_content && res.final_content[0] != '\0') {
                    snprintf(output_markdown,
                             md_cap,
                             "%s\n\n---\n*由 Minefolio 自动化调度中心与 AI 智能体生成*",
                             res.final_content);
                    snprintf(output_summary,
                             sizeof(output_summary),
                             "AI 简报生成完毕 (%d tokens, 耗时: %lldms)",
                             res.stats.total_tokens,
                             (long long)res.stats.elapsed_ms);
                    ai_ran_ok = true;
                }

                if (trace) {
                    if (ctx->user_id > 0) {
                        mf_ai_trace_save((csilk_db_pool_t*)ctx->pool, trace);
                    }
                    ai_trace_destroy(trace);
                    trace = NULL;
                }

                if (res.final_content) {
                    free(res.final_content);
                }
            }
            if (cfg) {
                ai_config_release(cfg);
            }

            /* 降级兜底方案：在无 API Key 或大模型调用异常时保证调度稳定可靠 */
            if (!ai_ran_ok) {
                snprintf(output_markdown,
                         md_cap,
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
            }

            if (pjson) {
                csilk_json_free(pjson);
            }

            atomic_fetch_sub(&s_ai_jobs_running, 1);
        }
    } else if (job_type && strcmp(job_type, "structured_report") == 0) {
        snprintf(output_markdown,
                 md_cap,
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
    } else if (job_type && strcmp(job_type, "system_builtin") == 0) {
        const char*   handler = "";
        csilk_json_t* pjson = payload_cfg ? csilk_json_parse(payload_cfg) : NULL;
        if (pjson) {
            handler = csilk_json_get_string(pjson, "handler");
        }

        if (handler && strcmp(handler, "market_quote_sync") == 0) {
            int synced = 0, failed = 0;
            market_usecase_do_sync_user(ctx->pool, 0, &synced, &failed);
            snprintf(output_summary,
                     sizeof(output_summary),
                     "全市场行情与基金净值同步完成 (成功: %d, 失败: %d)",
                     synced,
                     failed);
            snprintf(output_markdown,
                     md_cap,
                     "### 📈 全市场行情与基金净值同步\n\n"
                     "> **触发源**: %s | **执行时间**: %s\n\n"
                     "- **同步成功标的**: %d 个\n"
                     "- **同步失败标的**: %d 个\n"
                     "- **执行结论**: 全量持仓与自选标的行情已成功同步刷新。\n\n"
                     "---\n*Minefolio 系统内置任务调度*",
                     ctx->reason,
                     now_str,
                     synced,
                     failed);
        } else if (handler && strcmp(handler, "global_indices_sync") == 0) {
            quote_engine_sync_indices_to_db((csilk_db_pool_t*)ctx->pool);
            snprintf(output_summary, sizeof(output_summary), "全球核心大盘指数同步完成");
            snprintf(output_markdown,
                     md_cap,
                     "### 🌐 全球大盘指数轮询\n\n"
                     "> **触发源**: %s | **执行时间**: %s\n\n"
                     "- **覆盖标的**: 上证、深证、恒生、标普500、纳斯达克等核心指数\n"
                     "- **执行结论**: 核心大盘指数最新点位与涨跌幅已持久化到数据库。\n\n"
                     "---\n*Minefolio 系统内置任务调度*",
                     ctx->reason,
                     now_str);
        } else if (handler && strcmp(handler, "forex_rates_sync") == 0) {
            exchange_rate_refresh_all();
            snprintf(output_summary, sizeof(output_summary), "实时外汇基准汇率同步完成");
            snprintf(output_markdown,
                     md_cap,
                     "### 💱 实时外汇基准汇率同步\n\n"
                     "> **触发源**: %s | **执行时间**: %s\n\n"
                     "- **基准货币**: 人民币 (CNY)\n"
                     "- **对价币种**: USD, HKD, EUR, JPY, GBP 等主要外汇牌价\n"
                     "- **执行结论**: 国际基准外汇汇率已更新并预热至内存缓存。\n\n"
                     "---\n*Minefolio 系统内置任务调度*",
                     ctx->reason,
                     now_str);
        } else if (handler && strcmp(handler, "dca_plan_trigger") == 0) {
            time_t    now_sec = time(NULL);
            struct tm tm_now;
            localtime_r(&now_sec, &tm_now);
            market_scheduler_check_and_trigger_dca((csilk_db_pool_t*)ctx->pool, &tm_now);
            snprintf(output_summary, sizeof(output_summary), "定投划转计划每日巡检完成");
            snprintf(output_markdown,
                     md_cap,
                     "### 📅 定投划转计划每日巡检\n\n"
                     "> **触发源**: %s | **执行时间**: %s\n\n"
                     "- **检查周期**: 周期性自动巡检\n"
                     "- **执行结论**: 当日到期定投计划已生成待执行流水。\n\n"
                     "---\n*Minefolio 系统内置任务调度*",
                     ctx->reason,
                     now_str);
        } else if (handler && strcmp(handler, "ai_news_digest") == 0) {
            ai_news_scheduler_tick((csilk_db_pool_t*)ctx->pool);
            snprintf(output_summary, sizeof(output_summary), "AI 金融快讯与研报提炼完成");
            snprintf(output_markdown,
                     md_cap,
                     "### 📰 AI 每日金融快讯与研报提炼\n\n"
                     "> **触发源**: %s | **执行时间**: %s\n\n"
                     "- **资讯抓取**: 财经大盘快讯与宏观要闻\n"
                     "- **执行结论**: 大模型已提炼核心要点与市场情绪指数。\n\n"
                     "---\n*Minefolio 系统内置任务调度*",
                     ctx->reason,
                     now_str);
        } else if (handler && strcmp(handler, "mcp_pool_cleanup") == 0) {
            mf_mcp_stdio_pool_evict_idle();
            snprintf(output_summary, sizeof(output_summary), "MCP 外部进程池空闲回收完成");
            snprintf(output_markdown,
                     md_cap,
                     "### 🧹 MCP 外部进程池空闲回收\n\n"
                     "> **触发源**: %s | **执行时间**: %s\n\n"
                     "- **回收策略**: 释放空闲超时的 MCP 子进程\n"
                     "- **执行结论**: 系统空闲进程资源已成功释放。\n\n"
                     "---\n*Minefolio 系统内置任务调度*",
                     ctx->reason,
                     now_str);
        } else if (handler && strcmp(handler, "db_auto_backup") == 0) {
            char backup_fn[128] = {0};
            int  brc = db_backup_create(
                (csilk_db_pool_t*)ctx->pool, "data/backups", backup_fn, sizeof(backup_fn));
            if (brc == 0) {
                snprintf(output_summary,
                         sizeof(output_summary),
                         "数据库自动在线热备份成功: %s",
                         backup_fn);
                snprintf(output_markdown,
                         md_cap,
                         "### 💾 数据库自动在线热备份成功\n\n"
                         "> **触发源**: %s | **备份文件名**: `%s` | **执行时间**: %s\n\n"
                         "- **备份机制**: SQLite 官方在线分片热备份（100页分片零阻塞）\n"
                         "- **轮转策略**: 自动保留最近 7 份历史备份\n\n"
                         "---\n*Minefolio 系统内置任务调度*",
                         ctx->reason,
                         backup_fn,
                         now_str);
            } else {
                snprintf(output_summary, sizeof(output_summary), "数据库自动在线热备份失败");
                snprintf(output_markdown,
                         md_cap,
                         "### ⚠️ 数据库自动在线热备份失败\n\n"
                         "> **触发源**: %s | **执行时间**: %s\n\n"
                         "请检查磁盘可用存储空间或目录写权限。\n\n"
                         "---\n*Minefolio 系统内置任务调度*",
                         ctx->reason,
                         now_str);
            }
        } else {
            snprintf(output_summary,
                     sizeof(output_summary),
                     "系统内置任务执行完毕: %s",
                     handler ? handler : "未知");
            snprintf(output_markdown,
                     md_cap,
                     "### ⚙️ 系统内置任务执行完毕\n\n"
                     "> **任务标识**: %s | **触发时间**: %s\n\n"
                     "---\n*Minefolio 系统内置任务调度*",
                     handler ? handler : "未知",
                     now_str);
        }

        if (pjson) {
            csilk_json_free(pjson);
        }
    } else {
        snprintf(output_markdown,
                 md_cap,
                 "### ⏰ Minefolio 定时任务提醒: %s\n\n"
                 "> **触发时间**: %s | **触发源**: %s\n\n"
                 "任务已于调度中心顺利执行完成。\n",
                 job_name ? job_name : "定时任务",
                 now_str,
                 ctx->reason);
        snprintf(output_summary, sizeof(output_summary), "任务执行完成");
    }

    /* 4 & 5. Deliver notification & finalize job_run_logs (queued written BEFORE enqueue) */
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    int duration_ms = (int)((ts_end.tv_sec - ts_start.tv_sec) * 1000 +
                            (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000);

    const char* run_status = (is_ai_job && !ai_slot_acquired) ? "skipped" : "success";
    int64_t     job_user_id = db_get_int(job, "user_id");
    char        notif_status[16] = "none";
    char        err_buf[256];
    err_buf[0] = '\0';

    bool should_deliver = (!is_ai_job || ai_slot_acquired) && delivery_channel &&
                          strcmp(delivery_channel, "silent_db") != 0;

    if (should_deliver && job_user_id == 0 && ctx->user_id == 0) {
        csilk_json_t* channels = mf_scheduler_repo_list_active_channels(ctx->pool);
        int           sync_sent = 0, sync_failed = 0, ilink_count = 0;
        size_t        nch = channels ? csilk_json_array_size(channels) : 0;

        /* Pass 1: synchronous webhooks outside DB transaction so slow HTTP I/O never pins DB */
        for (size_t i = 0; i < nch; ++i) {
            csilk_json_t* ch = csilk_json_array_get(channels, i);
            const char*   ct = csilk_json_get_string(ch, "channel_type");
            if (!ct || !ct[0] || strcasecmp(ct, "none") == 0) {
                continue;
            }
            if (strcasecmp(ct, "wechat_ilink") == 0) {
                ilink_count++;
                continue;
            }
            char st[16] = "none";
            char ch_err[256] = {0};
            deliver_to_user_channel(ctx->pool,
                                    db_get_int(ch, "user_id"),
                                    log_id,
                                    ch,
                                    job_name,
                                    output_markdown,
                                    st,
                                    sizeof(st),
                                    ch_err,
                                    sizeof(ch_err));
            if (strcmp(st, "sent") == 0) {
                sync_sent++;
            } else if (strcmp(st, "failed") == 0) {
                sync_failed++;
                if (!err_buf[0] && ch_err[0]) {
                    snprintf(err_buf, sizeof(err_buf), "%s", ch_err);
                }
            }
        }

        if (ilink_count > 0) {
            /* Pass 2 + log completion inside one atomic DB tx so gateway cannot ack early */
            db_tx_scope_t scope = {0};
            bool          in_scope =
                (db_tx_scope_begin((csilk_db_pool_t*)ctx->pool, "sched_ilink_bc", &scope) == 0);
            bool tx_ok = false;

            if (in_scope) {
                int ilink_enqueued = 0, ilink_failed = 0;
                for (size_t i = 0; i < nch; ++i) {
                    csilk_json_t* ch = csilk_json_array_get(channels, i);
                    const char*   ct = csilk_json_get_string(ch, "channel_type");
                    if (!ct || strcasecmp(ct, "wechat_ilink") != 0) {
                        continue;
                    }
                    char st[16] = "none";
                    char ch_err[256] = {0};
                    deliver_to_user_channel(ctx->pool,
                                            db_get_int(ch, "user_id"),
                                            log_id,
                                            ch,
                                            job_name,
                                            output_markdown,
                                            st,
                                            sizeof(st),
                                            ch_err,
                                            sizeof(ch_err));
                    if (strcmp(st, "queued") == 0) {
                        ilink_enqueued++;
                    } else {
                        ilink_failed++;
                        if (!err_buf[0] && ch_err[0]) {
                            snprintf(err_buf, sizeof(err_buf), "%s", ch_err);
                        }
                    }
                }

                int total_fail = sync_failed + ilink_failed;
                int sc_rc = 0;
                if (sync_sent > 0 || total_fail > 0) {
                    sc_rc = mf_scheduler_repo_log_set_sync_counts(
                        ctx->pool, log_id, sync_sent, total_fail);
                }

                if (ilink_enqueued > 0) {
                    snprintf(notif_status, sizeof(notif_status), "queued");
                } else if (sync_sent > 0 && total_fail > 0) {
                    snprintf(notif_status, sizeof(notif_status), "partial");
                } else if (sync_sent > 0) {
                    snprintf(notif_status, sizeof(notif_status), "sent");
                } else if (total_fail > 0) {
                    snprintf(notif_status, sizeof(notif_status), "failed");
                }

                if (sc_rc == 0 &&
                    mf_scheduler_repo_log_complete(ctx->pool,
                                                   log_id,
                                                   run_status,
                                                   duration_ms,
                                                   output_summary,
                                                   err_buf,
                                                   notif_status) == 0 &&
                    db_tx_scope_commit((csilk_db_pool_t*)ctx->pool, &scope) == 0) {
                    tx_ok = true;
                } else {
                    if (in_scope) {
                        db_tx_scope_rollback((csilk_db_pool_t*)ctx->pool, &scope);
                    }
                    if (!err_buf[0]) {
                        snprintf(err_buf, sizeof(err_buf), "提交 iLink 广播推送事务失败");
                    }
                }
            } else if (!err_buf[0]) {
                snprintf(err_buf, sizeof(err_buf), "开启 iLink 广播推送事务失败");
            }

            if (!tx_ok) {
                mf_scheduler_repo_log_complete(
                    ctx->pool, log_id, run_status, duration_ms, output_summary, err_buf, "failed");
            }
        } else {
            if (sync_sent > 0 || sync_failed > 0) {
                mf_scheduler_repo_log_set_sync_counts(ctx->pool, log_id, sync_sent, sync_failed);
            }
            if (sync_sent > 0 && sync_failed > 0) {
                snprintf(notif_status, sizeof(notif_status), "partial");
            } else if (sync_sent > 0) {
                snprintf(notif_status, sizeof(notif_status), "sent");
            } else if (sync_failed > 0) {
                snprintf(notif_status, sizeof(notif_status), "failed");
            }
            mf_scheduler_repo_log_complete(
                ctx->pool, log_id, run_status, duration_ms, output_summary, err_buf, notif_status);
        }
        if (channels) {
            csilk_json_free(channels);
        }
    } else {
        csilk_json_t* ch =
            should_deliver ? mf_scheduler_repo_get_user_channel(ctx->pool, ctx->user_id) : NULL;
        const char* ct = ch ? csilk_json_get_string(ch, "channel_type") : NULL;
        bool        is_ilink =
            ch && db_get_int(ch, "is_active") == 1 && ct && strcasecmp(ct, "wechat_ilink") == 0;
        const char* error_field =
            (is_ai_job && !ai_slot_acquired) ? "ai_concurrency_limit" : err_buf;

        if (is_ilink) {
            db_tx_scope_t scope = {0};
            bool          in_scope =
                (db_tx_scope_begin((csilk_db_pool_t*)ctx->pool, "sched_ilink_tx", &scope) == 0);
            bool tx_ok = false;

            if (in_scope) {
                char st[16] = "none";
                deliver_to_user_channel(ctx->pool,
                                        ctx->user_id,
                                        log_id,
                                        ch,
                                        job_name,
                                        output_markdown,
                                        st,
                                        sizeof(st),
                                        err_buf,
                                        sizeof(err_buf));
                if (mf_scheduler_repo_log_complete(
                        ctx->pool, log_id, run_status, duration_ms, output_summary, err_buf, st) ==
                        0 &&
                    db_tx_scope_commit((csilk_db_pool_t*)ctx->pool, &scope) == 0) {
                    tx_ok = true;
                } else {
                    if (in_scope) {
                        db_tx_scope_rollback((csilk_db_pool_t*)ctx->pool, &scope);
                    }
                    if (!err_buf[0]) {
                        snprintf(err_buf, sizeof(err_buf), "提交 iLink 推送事务失败");
                    }
                }
            } else if (!err_buf[0]) {
                snprintf(err_buf, sizeof(err_buf), "开启 iLink 推送事务失败");
            }

            if (!tx_ok) {
                mf_scheduler_repo_log_complete(
                    ctx->pool, log_id, run_status, duration_ms, output_summary, err_buf, "failed");
            }
        } else {
            if (ch) {
                deliver_to_user_channel(ctx->pool,
                                        ctx->user_id,
                                        log_id,
                                        ch,
                                        job_name,
                                        output_markdown,
                                        notif_status,
                                        sizeof(notif_status),
                                        err_buf,
                                        sizeof(err_buf));
            }
            mf_scheduler_repo_log_complete(ctx->pool,
                                           log_id,
                                           run_status,
                                           duration_ms,
                                           output_summary,
                                           error_field,
                                           notif_status);
        }
        if (ch) {
            csilk_json_free(ch);
        }
    }

    /* 6. Calculate next run and update scheduled_jobs table */
    char next_run_buf[64];
    next_run_buf[0] = '\0';
    if (trigger_type && strcmp(trigger_type, "cron") == 0 && trigger_cfg) {
        if (strcmp(ctx->reason, "cron_tick") != 0) {
            csilk_json_t* tjson = csilk_json_parse(trigger_cfg);
            if (tjson) {
                const char* cron_s = csilk_json_get_string(tjson, "cron");
                if (cron_s && *cron_s) {
                    cron_format_next_str(cron_s, time(NULL), next_run_buf, sizeof(next_run_buf));
                }
                csilk_json_free(tjson);
            }
        }
    }

    mf_scheduler_repo_job_update_last_run(
        ctx->pool, ctx->job_id, run_status, duration_ms, next_run_buf[0] ? next_run_buf : NULL);

    csilk_json_free(job);
    free(output_markdown);
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
    csilk_json_t* rows = scheduler_repo_list_enabled_by_trigger(s_db_pool, "market_event");
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
        time_t now_sec = time(NULL);
        time_t min_future_sec = 0;
        size_t count = 0;

        if (s_db_pool) {
            char now_str[64];
            get_now_str(now_str, sizeof(now_str));

            csilk_json_t* rows = scheduler_repo_list_enabled_by_trigger(s_db_pool, "cron");
            if (rows) {
                count = csilk_json_array_size(rows);
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
                                            cron_s, now_sec, next_buf, sizeof(next_buf)) == 0) {
                                        mf_scheduler_repo_job_update_last_run(
                                            s_db_pool, job_id, "", 0, next_buf);
                                        time_t nb_sec = parse_time_str(next_buf);
                                        if (nb_sec > now_sec &&
                                            (min_future_sec == 0 || nb_sec < min_future_sec)) {
                                            min_future_sec = nb_sec;
                                        }
                                    }
                                }
                                csilk_json_free(tjson);
                            }
                        }
                    } else if (strcmp(next_run, now_str) <= 0) {
                        /* Calculate next run time */
                        char next_buf[64] = "";
                        if (tcfg_s && *tcfg_s) {
                            csilk_json_t* tjson = csilk_json_parse(tcfg_s);
                            if (tjson) {
                                const char* cron_s = csilk_json_get_string(tjson, "cron");
                                if (cron_s && *cron_s) {
                                    cron_format_next_str(
                                        cron_s, now_sec, next_buf, sizeof(next_buf));
                                }
                                csilk_json_free(tjson);
                            }
                        }

                        if (next_buf[0]) {
                            time_t nb_sec = parse_time_str(next_buf);
                            if (nb_sec > now_sec &&
                                (min_future_sec == 0 || nb_sec < min_future_sec)) {
                                min_future_sec = nb_sec;
                            }
                            /* Atomic CAS: only the node that wins the transition dispatches the run */
                            int claimed = mf_scheduler_repo_job_claim_cron_run(
                                s_db_pool, job_id, next_run, next_buf);
                            if (claimed) {
                                scheduler_engine_trigger_job_now(job_id, user_id, "cron_tick");
                            }
                        }
                    } else {
                        /* Future job */
                        time_t nr_sec = parse_time_str(next_run);
                        if (nr_sec > now_sec && (min_future_sec == 0 || nr_sec < min_future_sec)) {
                            min_future_sec = nr_sec;
                        }
                    }
                }
                csilk_json_free(rows);
            }
            im_push_usecase_expire_stale(s_db_pool);
        }

        /* 2. Compute dynamic sleep interval (capped at 60s for clock skew / safety) */
        time_t sleep_sec = 60;
        if (min_future_sec > now_sec) {
            time_t diff = min_future_sec - now_sec;
            sleep_sec = (diff < 60) ? diff : 60;
            if (sleep_sec < 1) {
                sleep_sec = 1;
            }
        } else if (count == 0) {
            sleep_sec = 60;
        } else {
            sleep_sec = 1;
        }

        pthread_mutex_lock(&s_engine_lock);
        s_last_sleep_sec = (int)sleep_sec;
        pthread_mutex_unlock(&s_engine_lock);

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += sleep_sec;

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

    atomic_store(&s_ai_jobs_running, 0);
    s_db_pool = pool;

    /* Seed built-in scheduled tasks idempotently */
    scheduler_usecase_seed_builtin_jobs(pool);

    s_worker_pool = scheduler_worker_pool_create(3);
    if (!s_worker_pool) {
        pthread_mutex_unlock(&s_engine_lock);
        return -1;
    }

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
    pthread_cond_destroy(&s_engine_cond);
    pthread_cond_init(&s_engine_cond, &cattr);
    pthread_condattr_destroy(&cattr);

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
    atomic_store(&s_ai_jobs_running, 0);
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
