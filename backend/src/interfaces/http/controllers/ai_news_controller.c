/**
 * @file ai_news_controller.c
 * @brief AI 每日资讯分析报告 HTTP 端点 (多账本 RBAC 与持仓隔离)
 *
 * GET  /api/ai/news?limit=7          获取指定账本最近报告列表
 * GET  /api/ai/news/:date            获取指定账本指定日期报告
 * POST /api/ai/news/trigger          手动触发指定账本分析
 */
#include "interfaces/http/controllers/ai_news_controller.h"
#include "services/ai/ai_news_service.h"
#include "common/ctx.h"
#include "common/response.h"
#include "common/db.h"
#include <csilk/csilk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
api_ai_news_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }

    int         limit = 7;
    const char* limit_str = csilk_get_query(c, "limit");
    if (limit_str && limit_str[0]) {
        int parsed = atoi(limit_str);
        if (parsed > 0 && parsed <= 30) {
            limit = parsed;
        }
    }

    int         offset = 0;
    const char* offset_str = csilk_get_query(c, "offset");
    if (offset_str && offset_str[0]) {
        int parsed = atoi(offset_str);
        if (parsed > 0) {
            offset = parsed;
        }
    }

    csilk_db_pool_t* pool = db_get_pool();
    int              has_more = 0;
    csilk_json_t* result = ai_news_list_ledger(pool, user_id, ledger_id, limit, offset, &has_more);
    if (!result) {
        result = csilk_json_array();
    }
    /* 包一层信封: { list, has_more } —— 分页加载需要 has_more 判断是否还有下一页 */
    csilk_json_t* envelope = csilk_json_object();
    csilk_json_add_array(envelope, "list", result);
    csilk_json_add_int(envelope, "has_more", has_more);
    respond_ok(c, envelope);
}

static void
api_ai_news_get_date(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }

    const char* date_str = csilk_get_param(c, "date");
    if (!date_str || !date_str[0] || strlen(date_str) != 10) {
        respond_bad_request(c, "Invalid report_date format");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    result = ai_news_get_by_date_ledger(pool, user_id, ledger_id, date_str);
    if (!result || csilk_json_array_size(result) == 0) {
        respond_not_found(c);
        csilk_json_free(result);
        return;
    }
    csilk_json_t* first = csilk_json_copy(csilk_json_array_get(result, 0));
    csilk_json_free(result);
    respond_ok(c, first);
}

static void
api_ai_news_trigger(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    int64_t          report_id = ai_news_trigger_ledger(pool, user_id, ledger_id);
    if (report_id < 0) {
        respond_error(c, 1004, "Failed to trigger news analysis");
        return;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_int(resp, "report_id", report_id);
    csilk_json_add_int(resp, "ledger_id", ledger_id);
    csilk_json_add_string(resp, "message", "分析任务已触发");
    respond_ok(c, resp);
}

void
register_ai_news_routes(csilk_app_t* app)
{
    if (!app) {
        return;
    }
    csilk_app_get(app, "/api/ai/news", api_ai_news_list);
    csilk_app_get(app, "/api/ai/news/:date", api_ai_news_get_date);
    csilk_app_post(app, "/api/ai/news/trigger", api_ai_news_trigger);
}
