/**
 * @file news_tool.c
 * @brief 市场脉搏 AI 工具：让 LLM 在对话中读取 AI 每日生成的市场分析报告。
 *
 * 工具 get_market_pulse：
 *   - 参数 days (int, 默认 1)：读取最近 N 天的报告
 *   - 参数 date (string, 可选)：指定某天 'YYYY-MM-DD'
 *   - 返回该用户对应日期的报告（市场概要 + 持仓影响 + 操作建议 + 风险提示）
 */
#include "services/ai/tools/news_tool.h"
#include "services/ai/tools/schema.h"
#include "services/ai/tools/registry.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char*
exec_get_market_pulse(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    /* 参数：date 优先，否则按 days 取最近 N 天 */
    const char* date_str = args ? csilk_json_get_string(args, "date") : NULL;
    long        days = 1;
    if (args) {
        const char* d_str = csilk_json_get_string(args, "days");
        if (d_str && d_str[0]) {
            days = atol(d_str);
        }
    }
    if (days < 1) {
        days = 1;
    }
    if (days > 14) {
        days = 14;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)ctx->user_id);

    int64_t ledger_id = 0;
    if (args) {
        const char* lid_str = csilk_json_get_string(args, "ledger_id");
        if (lid_str && lid_str[0]) {
            ledger_id = atoll(lid_str);
        }
    }
    if (ledger_id <= 0 && ctx->metadata) {
        ledger_id = db_get_int(ctx->metadata, "ledger_id");
    }
    char lid_str[32];
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    csilk_json_t* result = NULL;
    if (date_str && date_str[0]) {
        /* 指定日期 */
        result = csilk_db_query_param_json(
            ctx->pool,
            "SELECT report_date, title, analysis, status, error_msg, model_used, "
            "       created_at FROM ai_news_reports "
            "WHERE user_id=? AND (? <= 0 OR ledger_id=?) AND report_date=? LIMIT 1",
            (const char*[]){uid_str, lid_str, lid_str, date_str, NULL});
    } else {
        /* 最近 N 天 */
        char sql[512];
        snprintf(sql,
                 sizeof(sql),
                 "SELECT report_date, title, analysis, status, error_msg, model_used, "
                 "       created_at FROM ai_news_reports "
                 "WHERE user_id=? AND (? <= 0 OR ledger_id=?) AND status IN ('done','error') "
                 "ORDER BY report_date DESC LIMIT %ld",
                 days);
        result = csilk_db_query_param_json(
            ctx->pool, sql, (const char*[]){uid_str, lid_str, lid_str, NULL});
    }

    if (!result || csilk_json_array_size(result) == 0) {
        csilk_json_free(result);
        char msg[256];
        snprintf(msg,
                 sizeof(msg),
                 "{\"note\":\"尚无市场脉搏报告。可先用 web_search 检索资讯，"
                 "或提示用户先点击「市场脉搏」页面的立即生成。\"}");
        return strdup(msg);
    }

    /* 组装为对 LLM 友好的 JSON：数组元素为 {report_date, status, analysis} */
    size_t        n = csilk_json_array_size(result);
    csilk_json_t* out = csilk_json_array();
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(result, i);
        csilk_json_t* item = csilk_json_object();
        const char*   rd = csilk_json_get_string(row, "report_date");
        const char*   st = csilk_json_get_string(row, "status");
        const char*   an = csilk_json_get_string(row, "analysis");
        const char*   em = csilk_json_get_string(row, "error_msg");
        if (rd) {
            csilk_json_add_string(item, "report_date", rd);
        }
        csilk_json_add_string(item, "status", st ? st : "");
        if (st && strcmp(st, "done") == 0 && an) {
            csilk_json_add_string(item, "analysis", an);
        } else if (em) {
            csilk_json_add_string(item, "error", em);
        }
        csilk_json_array_append(out, item);
    }
    csilk_json_free(result);

    size_t len = 0;
    char*  serialized = csilk_json_serialize(out, &len);
    csilk_json_free(out);
    return serialized; /* caller frees */
}

void
ai_tool_news_register_all(void)
{
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(
            s, "date", "string", "指定报告日期 YYYY-MM-DD（可选；不填则按 days 取最近）");
        ai_schema_add_prop(s, "days", "number", "读取最近 N 天的报告，默认 1，最大 14");

        ai_tool_t t = {
            .name = "get_market_pulse",
            .description = "读取用户最近若干天由 AI 自动生成的「每日市场脉搏」分析"
                           "（市场概要、对持仓的影响、操作建议、风险提示）。当用户询问"
                           "「今天的市场/新闻对我的持仓有什么影响」「最近市场怎么样」时调用。",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_get_market_pulse,
        };
        ai_tool_register(&t);
    }
}
