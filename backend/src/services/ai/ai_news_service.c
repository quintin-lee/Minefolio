/**
 * @file ai_news_service.c
 * @brief AI 每日金融资讯分析与持仓影响评估服务。
 *
 * 流程：
 *   1. 查询用户当前持仓
 *   2. 获取当日金融新闻（模拟/外部 API）
 *   3. 调用 AI 模型综合分析，生成 Markdown 格式影响报告
 *   4. 持久化到 ai_news_reports 表
 */
#include "services/ai/ai_news_service.h"
#include "services/ai/runtime/loop.h"
#include "services/ai/runtime/context.h"
#include "services/ai/trace/trace.h"
#include "services/ai_service.h"
#include "common/db.h"
#include "domain/ai/rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 报告提示词模板 */
static const char* NEWS_ANALYSIS_PROMPT =
    "你是一位专业的个人投资顾问。请基于以下信息生成一份「每日市场脉搏」分析报告：\n\n"
    "## 当日金融资讯\n%s\n\n"
    "## 我的当前持仓\n%s\n\n"
    "请输出 Markdown 格式报告，包含：\n"
    "1. **市场概要**（2-3 句话总结当日宏观/行业要闻）\n"
    "2. **对持仓的具体影响**（逐条分析哪些持仓可能受益/受损，给出方向：利好/利空/中性）\n"
    "3. **操作建议**（是否需要调仓、减仓、加仓或观察，附简短理由）\n"
    "4. **风险提示**（当前需要关注的风险因素）\n\n"
    "保持专业、简洁、数据驱动。避免泛泛而谈。";

static void
get_local_date_str(char* out, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(out, sz, "%04d-%02d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
}

/**
 * @brief 执行单次 AI 分析并更新报告状态
 */
static int
ai_news_execute_analysis(csilk_db_pool_t* pool, int64_t user_id, int64_t report_id)
{
    /* 1. 获取用户持仓（非现金/非负债；类型由 categories.asset_type 承载） */
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    csilk_json_t* portfolio = csilk_db_query_param_json(
        pool,
        "SELECT a.name as asset_name, COALESCE(c.asset_type, '') as asset_type, "
        "  COALESCE(a.current_value, 0) as current_value, "
        "  COALESCE(a.cost_basis, 0) as cost_basis, "
        "  COALESCE(a.quantity, 0) as quantity "
        "FROM assets a LEFT JOIN categories c ON c.id = a.category_id "
        "WHERE a.user_id = ? AND COALESCE(c.asset_type, '') NOT IN "
        "  ('cash','loan','credit_card','other_liability') "
        "ORDER BY a.current_value DESC LIMIT 15",
        (const char*[]){uid_str, NULL});

    /* 2. 获取当日新闻（当前为模拟数据源，后续可接入真实 RSS/API） */
    const char* news_text = "今日要闻：\n"
                            "1. 美联储会议纪要显示多数官员支持 Q4 降息 25bp，美股三大期货高开\n"
                            "2. A 股沪指涨 1.2% 站上 3100 点，新能源/光伏板块领涨超 3%\n"
                            "3. 国际金价突破 2400 美元/盎司，创历史新高\n"
                            "4. 人民币汇率企稳回升至 7.10 附近\n"
                            "5. 国内 10 年期国债收益率下行至 2.15%，债市走强";

    /* 3. 构建 AI 上下文并执行 */
    char  prompt[8192];
    char* portfolio_str = "{}";
    if (portfolio && csilk_json_array_size(portfolio) > 0) {
        size_t plen = 0;
        portfolio_str = csilk_json_serialize(portfolio, &plen);
    }
    snprintf(prompt,
             sizeof(prompt),
             NEWS_ANALYSIS_PROMPT,
             news_text,
             portfolio_str && portfolio_str[0] ? portfolio_str : "(无持仓数据)");
    free(portfolio_str);
    csilk_json_free(portfolio);

    ai_runtime_context_t ctx;
    ai_runtime_context_init(&ctx);
    ctx.user_id = user_id;
    ctx.session_id = -1;
    ctx.limits.max_iterations = 3;
    ctx.limits.token_budget = 16384;
    ctx.temperature = 0.3;
    ctx.max_tokens = 2048;

    csilk_json_t* msgs = csilk_json_array();
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

    if (res.status.code != AI_RUNTIME_ERR_OK || !res.final_content) {
        /* 更新为 error 状态 */
        const char* err_msg = res.status.detail ? res.status.detail : "AI execution failed";
        char        id_str[32];
        snprintf(id_str, sizeof(id_str), "%lld", (long long)report_id);
        csilk_db_query_param_json(
            pool,
            "UPDATE ai_news_reports SET status='error', error_msg=? WHERE id=? AND user_id=?",
            (const char*[]){err_msg, id_str, uid_str, NULL});
        free(res.final_content);
        if (trace) {
            ai_trace_destroy(trace);
        }
        return -1;
    }

    /* 4. 更新报告为 done */
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)report_id);
    csilk_db_query_param_json(
        pool,
        "UPDATE ai_news_reports SET analysis=?, status='done' WHERE id=? AND user_id=?",
        (const char*[]){res.final_content, id_str, uid_str, NULL});

    free(res.final_content);
    if (trace) {
        mf_ai_trace_save(pool, trace);
        ai_trace_destroy(trace);
    }
    return 0;
}

csilk_json_t*
ai_news_list(csilk_db_pool_t* pool, int64_t user_id, int limit)
{
    if (!pool || user_id <= 0) {
        return csilk_json_array();
    }
    if (limit <= 0 || limit > 30) {
        limit = 7;
    }
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    char sql[256];
    snprintf(sql,
             sizeof(sql),
             "SELECT id, report_date, title, summary, analysis, affected_assets, "
             "       sources, model_used, token_cost, status, error_msg, created_at "
             "FROM ai_news_reports WHERE user_id=? ORDER BY report_date DESC LIMIT %d",
             limit);
    return csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, NULL});
}

csilk_json_t*
ai_news_get_by_date(csilk_db_pool_t* pool, int64_t user_id, const char* report_date)
{
    if (!pool || user_id <= 0 || !report_date || !report_date[0]) {
        return NULL;
    }
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT id, report_date, title, summary, analysis, affected_assets, "
        "       sources, model_used, token_cost, status, error_msg, created_at "
        "FROM ai_news_reports WHERE user_id=? AND report_date=? LIMIT 1",
        (const char*[]){uid_str, report_date, NULL});
}

int64_t
ai_news_trigger(csilk_db_pool_t* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    char date_str[16];
    get_local_date_str(date_str, sizeof(date_str));
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    /* 插入或重置当日 pending 记录（幂等；upsert 失败不影响幂等语义，由后续 SELECT 兜底） */
    csilk_db_query_param_json(
        pool,
        "INSERT INTO ai_news_reports (user_id, report_date, title, status) "
        "VALUES (?, ?, '正在生成今日市场脉搏…', 'pending') "
        "ON CONFLICT(user_id, report_date) DO UPDATE SET status='pending', error_msg=''",
        (const char*[]){uid_str, date_str, NULL});

    /* 获取记录 ID */
    csilk_json_t* row = csilk_db_query_param_json(
        pool,
        "SELECT id FROM ai_news_reports WHERE user_id=? AND report_date=?",
        (const char*[]){uid_str, date_str, NULL});

    int64_t report_id = -1;
    if (row && csilk_json_array_size(row) > 0) {
        report_id = db_get_int(csilk_json_array_get(row, 0), "id");
    }
    csilk_json_free(row);

    if (report_id > 0) {
        ai_news_execute_analysis(pool, user_id, report_id);
    }
    return report_id;
}

void
ai_news_scheduler_tick(csilk_db_pool_t* pool)
{
    if (!pool) {
        return;
    }
    csilk_json_t* users =
        csilk_db_query_json(pool,
                            "SELECT DISTINCT a.user_id FROM assets a "
                            "LEFT JOIN categories c ON c.id = a.category_id "
                            "WHERE COALESCE(c.asset_type, '') NOT IN "
                            "  ('cash','loan','credit_card','other_liability') LIMIT 50");
    if (!users) {
        return;
    }
    size_t cnt = csilk_json_array_size(users);
    for (size_t i = 0; i < cnt; i++) {
        csilk_json_t* item = csilk_json_array_get(users, i);
        int64_t       uid = db_get_int(item, "user_id");
        if (uid > 0) {
            ai_news_trigger(pool, uid);
        }
    }
    csilk_json_free(users);
}
