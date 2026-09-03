#include "interfaces/http/controllers/ai_trace_controller.h"
#include "application/ai/usecases.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
api_ai_trace_list_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);
    const char* provider = csilk_get_query(c, "provider");
    if (!provider) {
        provider = csilk_get_param(c, "provider");
    }
    const char* model = csilk_get_query(c, "model");
    if (!model) {
        model = csilk_get_param(c, "model");
    }

    csilk_json_t*       out_data = NULL;
    int64_t             total = 0;
    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_trace_list(
        db_get_pool(), user_id, page, page_size, provider, model, &out_data, &total, &res);
    if (rc == 0 && res.code == 0) {
        respond_page_ok(c, out_data, total, page, page_size);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_trace_stats_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t*       out_data = NULL;
    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_trace_stats(db_get_pool(), user_id, &out_data, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_trace_get_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }
    int64_t id = atoll(id_str);

    csilk_json_t*       out_data = NULL;
    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_trace_get(db_get_pool(), user_id, id, &out_data, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
register_ai_trace_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/ai/traces",
                      api_ai_trace_list_handler,
                      NULL,
                      NULL,
                      "List AI traces",
                      "Returns paginated AI conversation traces");
    csilk_app_get_ext(app,
                      "/api/ai/traces/stats",
                      api_ai_trace_stats_handler,
                      NULL,
                      NULL,
                      "AI trace stats",
                      "Returns aggregate trace statistics");
    csilk_app_get_ext(app,
                      "/api/ai/traces/:id",
                      api_ai_trace_get_handler,
                      NULL,
                      NULL,
                      "Get AI trace",
                      "Returns full trace detail including messages");
}
