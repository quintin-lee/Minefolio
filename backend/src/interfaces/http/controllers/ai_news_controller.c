/**
 * @file ai_news_controller.c
 * @brief AI 每日资讯分析报告 HTTP 端点
 *
 * GET  /api/ai/news?limit=7          获取最近报告列表
 * GET  /api/ai/news/:date            获取指定日期报告
 * POST /api/ai/news/trigger          手动触发一次分析
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
        respond_unauthorized(c);
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

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    result = ai_news_list(pool, user_id, limit);
    respond_ok(c, result);
}

static void
api_ai_news_get_date(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }

    const char* date_str = csilk_get_param(c, "date");
    if (!date_str || !date_str[0] || strlen(date_str) != 10) {
        respond_bad_request(c, "Invalid report_date format");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    result = ai_news_get_by_date(pool, user_id, date_str);
    if (!result || csilk_json_array_size(result) == 0) {
        respond_not_found(c);
        csilk_json_free(result);
        return;
    }
    csilk_json_t* first = csilk_json_array_get(result, 0);
    respond_ok(c, first);
    csilk_json_free(result);
}

static void
api_ai_news_trigger(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    int64_t          report_id = ai_news_trigger(pool, user_id);
    if (report_id < 0) {
        respond_error(c, 1004, "Failed to trigger news analysis");
        return;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_int(resp, "report_id", report_id);
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
