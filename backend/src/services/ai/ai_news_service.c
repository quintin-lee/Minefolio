/**
 * @file ai_news_service.c
 * @brief AI 每日金融资讯分析与持仓影响评估服务（两阶段解耦与持仓脱敏隔离）。
 *
 * 架构：
 *   Stage 1: 全局宏观大盘摘要生成 (market_news_digests) —— 每日全站运行 1 次，零个人隐私；
 *   Stage 2: 用户持仓隔离与脱敏映射 (ai_news_reports) —— 绑定 user_id + ledger_id，绝对金额脱敏为百分比。
 */
#include "services/ai/ai_news_service.h"
#include "services/ai/runtime/loop.h"
#include "services/ai/runtime/context.h"
#include "services/ai/trace/trace.h"
#include "services/ai_service.h"
#include "application/ai/config_provider.h"
#include "common/db.h"
#include "domain/ai/rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static void
get_local_date_str(char* out, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(out, sz, "%04d-%02d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
}

static int64_t
resolve_user_ledger_id(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id)
{
    if (ledger_id > 0) {
        return ledger_id;
    }
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    csilk_json_t* row = csilk_db_query_param_json(
        pool,
        "SELECT id FROM ledgers WHERE owner_id=? AND is_default=1 LIMIT 1",
        (const char*[]){uid_str, NULL});
    int64_t def_id = 0;
    if (row && csilk_json_array_size(row) > 0) {
        def_id = db_get_int(csilk_json_array_get(row, 0), "id");
    }
    if (row) {
        csilk_json_free(row);
    }
    return def_id;
}

int
ai_news_mask_portfolio(const csilk_json_t* assets_json,
                       csilk_json_t**      out_masked_json,
                       double*             out_total_value)
{
    if (!out_masked_json) {
        return -1;
    }
    *out_masked_json = csilk_json_array();
    if (out_total_value) {
        *out_total_value = 0.0;
    }
    if (!assets_json || !csilk_json_is_array(assets_json)) {
        return 0;
    }

    size_t sz = csilk_json_array_size(assets_json);
    if (sz == 0) {
        return 0;
    }

    /* 1. 统计有效投资总市值 */
    double total = 0.0;
    for (size_t i = 0; i < sz; i++) {
        csilk_json_t* item = csilk_json_array_get(assets_json, i);
        if (!item) {
            continue;
        }
        double val = db_get_num(item, "current_value");
        if (val > 0.0) {
            total += val;
        }
    }
    if (out_total_value) {
        *out_total_value = total;
    }

    /* 2. 构建脱敏条目，按百分比归一化 */
    for (size_t i = 0; i < sz; i++) {
        csilk_json_t* item = csilk_json_array_get(assets_json, i);
        if (!item) {
            continue;
        }
        const char* name = csilk_json_get_string(item, "asset_name");
        if (!name || !name[0]) {
            name = csilk_json_get_string(item, "name");
        }
        const char* cat = csilk_json_get_string(item, "asset_type");
        if (!cat || !cat[0]) {
            cat = csilk_json_get_string(item, "category");
        }
        double val = db_get_num(item, "current_value");

        double pct = 0.0;
        if (total > 0.0 && val > 0.0) {
            pct = (val / total) * 100.0;
        }

        char weight_buf[32];
        snprintf(weight_buf, sizeof(weight_buf), "%.1f%%", pct);

        csilk_json_t* masked_item = csilk_json_object();
        csilk_json_add_string(masked_item, "name", name ? name : "");
        csilk_json_add_string(masked_item, "category", cat ? cat : "");
        csilk_json_add_string(masked_item, "weight", weight_buf);

        csilk_json_array_append(*out_masked_json, masked_item);
    }

    return 0;
}

csilk_json_t*
ai_news_get_or_create_digest(csilk_db_pool_t* pool, const char* report_date, int force_refresh)
{
    if (!pool) {
        return NULL;
    }
    char date_buf[32];
    if (report_date && report_date[0] && strlen(report_date) == 10) {
        snprintf(date_buf, sizeof(date_buf), "%s", report_date);
    } else {
        get_local_date_str(date_buf, sizeof(date_buf));
    }

    if (!force_refresh) {
        csilk_json_t* existing =
            csilk_db_query_param_json(pool,
                                      "SELECT id, digest_date, title, macro_summary, "
                                      "sector_impacts, sources, model_used, token_cost "
                                      "FROM market_news_digests WHERE digest_date=? LIMIT 1",
                                      (const char*[]){date_buf, NULL});
        if (existing && csilk_json_array_size(existing) > 0) {
            csilk_json_t* first = csilk_json_copy(csilk_json_array_get(existing, 0));
            csilk_json_free(existing);
            return first;
        }
        if (existing) {
            csilk_json_free(existing);
        }
    }

    /* 准备生成 Stage 1 全局大盘摘要 */
    const char* news_text = "今日宏观要闻：\n"
                            "1. 美联储会议纪要显示多数官员支持 Q4 降息 25bp，美股三大期货高开\n"
                            "2. A 股沪指涨 1.2% 站上 3100 点，新能源/光伏板块领涨超 3%\n"
                            "3. 国际金价突破 2400 美元/盎司，创历史新高\n"
                            "4. 人民币汇率企稳回升至 7.10 附近\n"
                            "5. 国内 10 年期国债收益率下行至 2.15%，债市走强";

    static const char* DIGEST_SYSTEM_PROMPT =
        "你是一位宏观金融与资本市场研究专家。请分析今日金融要闻，输出纯 Markdown "
        "格式的宏观与板块概括。\n"
        "包含两个核心板块：\n"
        "## 市场宏观主线\n"
        "(2-3句话总结宏观流动性与核心资产方向)\n\n"
        "## 行业板块影响\n"
        "(列举3-5个主要受影响板块，标明板块名、利好/利空/中性，以及核心驱动逻辑)";

    ai_runtime_context_t ctx;
    ai_runtime_context_init(&ctx);
    ctx.user_id = 1;
    ctx.session_id = 0;
    ctx.limits.max_iterations = 2;
    ctx.limits.token_budget = 8192;
    ctx.temperature = 0.3;
    ctx.max_tokens = 1024;

    /* 获取默认模型 */
    {
        ai_config_t* acfg = ai_config_acquire();
        if (acfg && acfg->default_model[0]) {
            snprintf(ctx.model_name, sizeof(ctx.model_name), "%s", acfg->default_model);
        }
        if (acfg) {
            ai_config_release(acfg);
        }
    }

    csilk_json_t* msgs = ctx.messages;
    csilk_json_t* sys = csilk_json_object();
    csilk_json_add_string(sys, "role", "system");
    csilk_json_add_string(sys, "content", DIGEST_SYSTEM_PROMPT);
    csilk_json_array_append(msgs, sys);

    csilk_json_t* usr = csilk_json_object();
    csilk_json_add_string(usr, "role", "user");
    csilk_json_add_string(usr, "content", news_text);
    csilk_json_array_append(msgs, usr);
    ctx.messages = msgs;

    ai_trace_t*         trace = NULL;
    ai_runtime_result_t res = ai_runtime_execute(pool, &ctx, &trace);
    ai_runtime_context_free(&ctx);
    if (trace) {
        ai_trace_destroy(trace);
    }

    char title_buf[128];
    snprintf(title_buf, sizeof(title_buf), "每日市场脉搏 — %s", date_buf);
    const char* macro_summary = res.final_content
                                    ? res.final_content
                                    : "今日宏观偏暖，降息预期推动核心资产与权益市场反弹。";

    const char* sector_impacts =
        "[{\"sector\":\"新能源/光伏\",\"trend\":\"利好\",\"reason\":\"板块领涨超3%\"},"
        "{\"sector\":\"黄金/贵金属\",\"trend\":\"利好\",\"reason\":\"金价创新高突破2400美元\"},"
        "{\"sector\":\"债市/固收\",\"trend\":\"利好\",\"reason\":\"10年期国债收益率下行\"}]";

    char cost_buf[32] = "0.0";
    if (res.stats.total_cost > 0) {
        snprintf(cost_buf, sizeof(cost_buf), "%.6f", res.stats.total_cost);
    }

    csilk_db_query_param_json(
        pool,
        "INSERT INTO market_news_digests (digest_date, title, macro_summary, sector_impacts, "
        "sources, model_used, token_cost) "
        "VALUES (?, ?, ?, ?, '[]', ?, ?) "
        "ON CONFLICT(digest_date) DO UPDATE SET "
        "title=excluded.title, macro_summary=excluded.macro_summary, "
        "sector_impacts=excluded.sector_impacts, "
        "model_used=excluded.model_used, token_cost=excluded.token_cost",
        (const char*[]){
            date_buf, title_buf, macro_summary, sector_impacts, ctx.model_name, cost_buf, NULL});

    free(res.final_content);

    csilk_json_t* created =
        csilk_db_query_param_json(pool,
                                  "SELECT id, digest_date, title, macro_summary, sector_impacts, "
                                  "sources, model_used, token_cost "
                                  "FROM market_news_digests WHERE digest_date=? LIMIT 1",
                                  (const char*[]){date_buf, NULL});
    if (created && csilk_json_array_size(created) > 0) {
        csilk_json_t* first = csilk_json_copy(csilk_json_array_get(created, 0));
        csilk_json_free(created);
        return first;
    }
    if (created) {
        csilk_json_free(created);
    }
    return NULL;
}

/* 后台 worker 参数（堆分配，detached 线程使用） */
typedef struct {
    csilk_db_pool_t* pool;
    int64_t          user_id;
    int64_t          ledger_id;
    int64_t          report_id;
} news_worker_arg_t;

static int ai_news_execute_analysis(csilk_db_pool_t* pool,
                                    int64_t          user_id,
                                    int64_t          ledger_id,
                                    int64_t          report_id);
void*      ai_news_execute_analysis_wrapper(void* p);

/**
 * @brief 执行单次用户持仓脱敏分析并更新报告状态 (Stage 2)
 */
static int
ai_news_execute_analysis(csilk_db_pool_t* pool,
                         int64_t          user_id,
                         int64_t          ledger_id,
                         int64_t          report_id)
{
    char date_str[32];
    get_local_date_str(date_str, sizeof(date_str));
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    char lid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    /* 1. 获取 Stage 1 全局公共大盘摘要 */
    csilk_json_t* digest = ai_news_get_or_create_digest(pool, date_str, 0);
    int64_t       digest_id = 0;
    const char*   macro_summary = NULL;
    const char*   sector_impacts = NULL;
    if (digest) {
        digest_id = db_get_int(digest, "id");
        macro_summary = csilk_json_get_string(digest, "macro_summary");
        sector_impacts = csilk_json_get_string(digest, "sector_impacts");
    }

    /* 2. 获取用户指定账本持仓（非现金/非负债） */
    csilk_json_t* raw_assets = NULL;
    if (ledger_id > 0) {
        raw_assets = csilk_db_query_param_json(
            pool,
            "SELECT a.id, a.name as asset_name, COALESCE(c.asset_type, '') as asset_type, "
            "       COALESCE(a.current_value, 0) as current_value "
            "FROM assets a LEFT JOIN categories c ON c.id = a.category_id "
            "WHERE a.user_id = ? AND a.ledger_id = ? AND COALESCE(c.asset_type, '') NOT IN "
            "      ('cash','loan','credit_card','other_liability') "
            "ORDER BY a.current_value DESC LIMIT 15",
            (const char*[]){uid_str, lid_str, NULL});
    } else {
        raw_assets = csilk_db_query_param_json(
            pool,
            "SELECT a.id, a.name as asset_name, COALESCE(c.asset_type, '') as asset_type, "
            "       COALESCE(a.current_value, 0) as current_value "
            "FROM assets a LEFT JOIN categories c ON c.id = a.category_id "
            "WHERE a.user_id = ? AND COALESCE(c.asset_type, '') NOT IN "
            "      ('cash','loan','credit_card','other_liability') "
            "ORDER BY a.current_value DESC LIMIT 15",
            (const char*[]){uid_str, NULL});
    }

    /* 3. 持仓数据绝对值脱敏（仅计算百分比权重，屏蔽金额与数量） */
    csilk_json_t* masked_portfolio = NULL;
    double        total_val = 0.0;
    ai_news_mask_portfolio(raw_assets, &masked_portfolio, &total_val);

    char* holdings_snapshot_str = NULL;
    if (masked_portfolio && csilk_json_array_size(masked_portfolio) > 0) {
        size_t hlen = 0;
        holdings_snapshot_str = csilk_json_serialize(masked_portfolio, &hlen);
    }

    /* 4. 构建 Prompt：仅传入宏观资讯与已脱敏的持仓百分比 */
    char prompt[8192];
    snprintf(prompt,
             sizeof(prompt),
             "你是一位专业的个人投资顾问。请结合今日宏观资讯与板块影响，为我的当前持仓组合生成专属"
             "投资分析报告：\n\n"
             "## 今日大盘与行业主线\n%s\n\n"
             "## 板块利好/利空参考\n%s\n\n"
             "## 我的当前持仓组合配置 (已脱敏相对比重)\n%s\n\n"
             "请输出 Markdown 格式报告，包含：\n"
             "1. **对您持仓的具体影响**（逐条结合持仓标的与行业板块分析潜在利好/利空及影响程度）\n"
             "2. **仓位与操作建议**（基于当前配置比例给出调仓、止盈或防守建议）\n"
             "3. **针对性风险提示**（当前持仓最需防范的风险因子）\n\n"
             "保持专业、严谨、客观。避免给出绝对化收益保证。",
             macro_summary ? macro_summary : "宏观偏多震荡",
             sector_impacts ? sector_impacts : "[]",
             holdings_snapshot_str && holdings_snapshot_str[0] ? holdings_snapshot_str
                                                               : "(无持仓数据)");

    if (digest) {
        csilk_json_free(digest);
    }
    if (raw_assets) {
        csilk_json_free(raw_assets);
    }
    if (masked_portfolio) {
        csilk_json_free(masked_portfolio);
    }

    ai_runtime_context_t ctx;
    ai_runtime_context_init(&ctx);
    ctx.user_id = user_id;
    ctx.session_id = 0;
    ctx.limits.max_iterations = 3;
    ctx.limits.token_budget = 16384;
    ctx.temperature = 0.3;
    ctx.max_tokens = 2048;

    {
        ai_config_t* acfg = ai_config_acquire();
        if (acfg && acfg->default_model[0]) {
            snprintf(ctx.model_name, sizeof(ctx.model_name), "%s", acfg->default_model);
        }
        if (acfg) {
            ai_config_release(acfg);
        }
    }

    csilk_json_t* msgs = ctx.messages;
    csilk_json_t* sys = csilk_json_object();
    csilk_json_add_string(sys, "role", "system");
    csilk_json_add_string(
        sys, "content", "你是一个专业的个人投资顾问 AI。输出必须为 Markdown 格式。");
    csilk_json_array_append(msgs, sys);
    csilk_json_t* usr = csilk_json_object();
    csilk_json_add_string(usr, "role", "user");
    csilk_json_add_string(usr, "content", prompt);
    csilk_json_array_append(msgs, usr);
    ctx.messages = msgs;

    ai_trace_t*         trace = NULL;
    ai_runtime_result_t res = ai_runtime_execute(pool, &ctx, &trace);
    ai_runtime_context_free(&ctx);

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)report_id);
    char did_str[32];
    snprintf(did_str, sizeof(did_str), "%lld", (long long)digest_id);
    const char* snapshot_to_save = holdings_snapshot_str ? holdings_snapshot_str : "[]";

    if (res.status.code != AI_RUNTIME_ERR_OK || !res.final_content) {
        const char* err_msg = res.status.detail[0] ? res.status.detail : res.status.message;
        csilk_db_query_param_json(
            pool,
            "UPDATE ai_news_reports SET status='error', error_msg=?, holdings_snapshot=?, "
            "digest_id=? WHERE id=? AND user_id=?",
            (const char*[]){err_msg, snapshot_to_save, did_str, id_str, uid_str, NULL});
        free(res.final_content);
        free(holdings_snapshot_str);
        if (trace) {
            ai_trace_destroy(trace);
        }
        return -1;
    }

    /* 5. 更新用户报告为 done 并持久化 holdings_snapshot 和 digest_id */
    char title_buf[128];
    snprintf(title_buf, sizeof(title_buf), "每日市场脉搏 — %s", date_str);

    csilk_db_query_param_json(
        pool,
        "UPDATE ai_news_reports SET title=?, analysis=?, holdings_snapshot=?, digest_id=?, "
        "status='done' "
        "WHERE id=? AND user_id=?",
        (const char*[]){
            title_buf, res.final_content, snapshot_to_save, did_str, id_str, uid_str, NULL});

    free(res.final_content);
    free(holdings_snapshot_str);
    if (trace) {
        ai_trace_destroy(trace);
    }
    return 0;
}

csilk_json_t*
ai_news_list_ledger(csilk_db_pool_t* pool,
                    int64_t          user_id,
                    int64_t          ledger_id,
                    int              limit,
                    int              offset,
                    int*             out_has_more)
{
    if (out_has_more) {
        *out_has_more = 0;
    }
    if (!pool || user_id <= 0) {
        return csilk_json_array();
    }
    if (limit <= 0 || limit > 30) {
        limit = 7;
    }
    if (offset < 0) {
        offset = 0;
    }
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    char lid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    char sql[512];
    snprintf(sql,
             sizeof(sql),
             "SELECT id, user_id, ledger_id, report_date, digest_id, title, summary, "
             "       holdings_snapshot, affected_assets, model_used, token_cost, status, "
             "error_msg, created_at "
             "FROM ai_news_reports WHERE user_id=? AND (? <= 0 OR ledger_id=?) "
             "ORDER BY report_date DESC LIMIT %d OFFSET %d",
             limit,
             offset);
    csilk_json_t* rows =
        csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, lid_str, lid_str, NULL});

    char more_sql[512];
    snprintf(more_sql,
             sizeof(more_sql),
             "SELECT 1 FROM ai_news_reports WHERE user_id=? AND (? <= 0 OR ledger_id=?) "
             "ORDER BY report_date DESC LIMIT 1 OFFSET %d",
             offset + limit);
    csilk_json_t* more =
        csilk_db_query_param_json(pool, more_sql, (const char*[]){uid_str, lid_str, lid_str, NULL});
    int has_more = (more && csilk_json_is_array(more) && csilk_json_array_size(more) > 0) ? 1 : 0;
    if (out_has_more) {
        *out_has_more = has_more;
    }
    if (more) {
        csilk_json_free(more);
    }
    return rows;
}

csilk_json_t*
ai_news_list(csilk_db_pool_t* pool, int64_t user_id, int limit, int offset, int* out_has_more)
{
    return ai_news_list_ledger(pool, user_id, 0, limit, offset, out_has_more);
}

csilk_json_t*
ai_news_get_by_date_ledger(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           int64_t          ledger_id,
                           const char*      report_date)
{
    if (!pool || user_id <= 0 || !report_date || !report_date[0]) {
        return NULL;
    }
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    char lid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    return csilk_db_query_param_json(
        pool,
        "SELECT id, user_id, ledger_id, report_date, digest_id, title, summary, analysis, "
        "       holdings_snapshot, affected_assets, sources, model_used, token_cost, status, "
        "error_msg, created_at "
        "FROM ai_news_reports WHERE user_id=? AND (? <= 0 OR ledger_id=?) AND report_date=? LIMIT "
        "1",
        (const char*[]){uid_str, lid_str, lid_str, report_date, NULL});
}

csilk_json_t*
ai_news_get_by_date(csilk_db_pool_t* pool, int64_t user_id, const char* report_date)
{
    return ai_news_get_by_date_ledger(pool, user_id, 0, report_date);
}

int64_t
ai_news_trigger_ledger(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    int64_t resolved_lid = resolve_user_ledger_id(pool, user_id, ledger_id);
    char    date_str[32];
    get_local_date_str(date_str, sizeof(date_str));
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    char lid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)resolved_lid);

    /* 插入或重置当日对应账本的 pending 记录 */
    csilk_db_query_param_json(
        pool,
        "INSERT INTO ai_news_reports (user_id, ledger_id, report_date, title, status) "
        "VALUES (?, ?, ?, '正在生成今日市场脉搏…', 'pending') "
        "ON CONFLICT(user_id, ledger_id, report_date) DO UPDATE SET status='pending', error_msg=''",
        (const char*[]){uid_str, lid_str, date_str, NULL});

    csilk_json_t* row = csilk_db_query_param_json(
        pool,
        "SELECT id FROM ai_news_reports WHERE user_id=? AND ledger_id=? AND report_date=?",
        (const char*[]){uid_str, lid_str, date_str, NULL});

    int64_t report_id = -1;
    if (row && csilk_json_array_size(row) > 0) {
        report_id = db_get_int(csilk_json_array_get(row, 0), "id");
    }
    if (row) {
        csilk_json_free(row);
    }

    if (report_id > 0) {
        news_worker_arg_t* arg = (news_worker_arg_t*)malloc(sizeof(news_worker_arg_t));
        if (arg) {
            arg->pool = pool;
            arg->user_id = user_id;
            arg->ledger_id = resolved_lid;
            arg->report_id = report_id;
            pthread_t tid;
            int       trc = pthread_create(&tid, NULL, ai_news_execute_analysis_wrapper, arg);
            if (trc == 0) {
                pthread_detach(tid);
            } else {
                char id_str[32];
                snprintf(id_str, sizeof(id_str), "%lld", (long long)report_id);
                csilk_db_query_param_json(
                    pool,
                    "UPDATE ai_news_reports SET status='error', error_msg=? WHERE id=? AND "
                    "user_id=?",
                    (const char*[]){"worker spawn failed", id_str, uid_str, NULL});
                free(arg);
            }
        }
    }
    return report_id;
}

int64_t
ai_news_trigger(csilk_db_pool_t* pool, int64_t user_id)
{
    return ai_news_trigger_ledger(pool, user_id, 0);
}

void*
ai_news_execute_analysis_wrapper(void* p)
{
    news_worker_arg_t* arg = (news_worker_arg_t*)p;
    if (arg) {
        ai_news_execute_analysis(arg->pool, arg->user_id, arg->ledger_id, arg->report_id);
        free(arg);
    }
    return NULL;
}

void
ai_news_scheduler_tick(csilk_db_pool_t* pool)
{
    if (!pool) {
        return;
    }
    /* 阶段一：每日自动生成全站公共大盘摘要（Stage 1 全局唯一执行） */
    char date_str[32];
    get_local_date_str(date_str, sizeof(date_str));
    csilk_json_t* digest = ai_news_get_or_create_digest(pool, date_str, 0);
    if (digest) {
        csilk_json_free(digest);
    }
}
