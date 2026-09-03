#include "interfaces/http/controllers/ai_controller.h"
#include "application/ai/usecases.h"
#include "services/ai_service.h"
#include "services/ai_workflow_service.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
api_ai_models_handler(csilk_ctx_t* c)
{
    csilk_json_t*       out_data = NULL;
    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_models_list(&out_data, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_chat_handler(csilk_ctx_t* c)
{
    ai_chat_handler(c);
}

void
api_ai_sessions_list_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    csilk_json_t*       out_data = NULL;
    int64_t             total = 0;
    ai_usecase_result_t res = {0};
    int                 rc =
        ai_usecase_sessions_list(db_get_pool(), user_id, page, page_size, &out_data, &total, &res);
    if (rc == 0 && res.code == 0) {
        respond_page_ok(c, out_data, total, page, page_size);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_sessions_create_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t*           body = csilk_bind_json(c);
    ai_create_session_cmd_t cmd = {
        .user_id = user_id,
        .title = csilk_json_get_string(body, "title"),
        .model = csilk_json_get_string(body, "model"),
        .provider = csilk_json_get_string(body, "provider"),
    };

    int64_t             new_id = 0;
    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_sessions_create(db_get_pool(), &cmd, &new_id, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* r = csilk_json_object();
        csilk_json_add_number(r, "id", (double)new_id);
        respond_ok(c, r);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_sessions_get_handler(csilk_ctx_t* c)
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
    int                 rc = ai_usecase_sessions_get(db_get_pool(), user_id, id, &out_data, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_sessions_update_handler(csilk_ctx_t* c)
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

    csilk_json_t*           body = csilk_bind_json(c);
    ai_update_session_cmd_t cmd = {
        .user_id = user_id,
        .id = id,
        .title = csilk_json_get_string(body, "title"),
        .model = csilk_json_get_string(body, "model"),
    };

    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_sessions_update(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_sessions_delete_handler(csilk_ctx_t* c)
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

    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_sessions_delete(db_get_pool(), user_id, id, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_messages_list_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 session_id");
        return;
    }
    int64_t session_id = atoll(id_str);

    int64_t page = 1, page_size = 50;
    parse_page_params(c, &page, &page_size);

    csilk_json_t*       out_data = NULL;
    int64_t             total = 0;
    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_messages_list(
        db_get_pool(), user_id, session_id, page, page_size, &out_data, &total, &res);
    if (rc == 0 && res.code == 0) {
        respond_page_ok(c, out_data, total, page, page_size);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_settings_get_handler(csilk_ctx_t* c)
{
    csilk_json_t*       out_data = NULL;
    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_settings_get(&out_data, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_settings_update_handler(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_settings_update(db_get_pool(), body, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_test_connection_handler(csilk_ctx_t* c)
{
    ai_service_test_connection(c);
}

void
api_ai_fetch_models_handler(csilk_ctx_t* c)
{
    ai_service_fetch_models(c);
}

void
api_ai_workflows_list_handler(csilk_ctx_t* c)
{
    csilk_json_t*       out_data = NULL;
    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_workflows_list(&out_data, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_workflows_run_handler(csilk_ctx_t* c)
{
    ai_workflow_run_handler(c);
}

void
register_ai_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/ai/models",
                      api_ai_models_handler,
                      NULL,
                      NULL,
                      "List AI models",
                      "Returns available models from configured providers");
    csilk_app_post_ext(app,
                       "/api/ai/chat",
                       api_ai_chat_handler,
                       NULL,
                       NULL,
                       "Chat (SSE)",
                       "Streaming chat endpoint");
    csilk_app_get_ext(
        app, "/api/ai/sessions", api_ai_sessions_list_handler, NULL, NULL, "List sessions", "");
    csilk_app_post_ext(
        app, "/api/ai/sessions", api_ai_sessions_create_handler, NULL, NULL, "Create session", "");
    csilk_app_get_ext(
        app, "/api/ai/sessions/:id", api_ai_sessions_get_handler, NULL, NULL, "Get session", "");
    csilk_app_put_ext(app,
                      "/api/ai/sessions/:id",
                      api_ai_sessions_update_handler,
                      NULL,
                      NULL,
                      "Update session",
                      "");
    csilk_app_delete_ext(app,
                         "/api/ai/sessions/:id",
                         api_ai_sessions_delete_handler,
                         NULL,
                         NULL,
                         "Delete session",
                         "");
    csilk_app_get_ext(app,
                      "/api/ai/sessions/:id/messages",
                      api_ai_messages_list_handler,
                      NULL,
                      NULL,
                      "List messages",
                      "");
    csilk_app_get_ext(
        app, "/api/settings/ai", api_ai_settings_get_handler, NULL, NULL, "Get AI config", "");
    csilk_app_put_ext(app,
                      "/api/settings/ai",
                      api_ai_settings_update_handler,
                      NULL,
                      NULL,
                      "Update AI config",
                      "");
    csilk_app_post_ext(app,
                       "/api/settings/ai/test",
                       api_ai_test_connection_handler,
                       NULL,
                       NULL,
                       "Test AI connection",
                       "Tests connectivity to an AI provider");
    csilk_app_post_ext(app,
                       "/api/settings/ai/fetch-models",
                       api_ai_fetch_models_handler,
                       NULL,
                       NULL,
                       "Fetch AI models",
                       "Fetches available models from provider");
    csilk_app_get_ext(app,
                      "/api/ai/workflows",
                      api_ai_workflows_list_handler,
                      NULL,
                      NULL,
                      "List AI workflows",
                      "Returns preset financial workflows");
    csilk_app_post_ext(app,
                       "/api/ai/workflows/run",
                       api_ai_workflows_run_handler,
                       NULL,
                       NULL,
                       "Run AI workflow (SSE)",
                       "Streaming financial workflow execution");
}
