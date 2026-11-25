#include "interfaces/http/controllers/im_bot_instance_controller.h"

#include "application/ai/im_bot_instance_usecases.h"
#include "application/task/async_task_manager.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/response.h"
#include "interfaces/http/controllers/im_bot_controller.h"
#include "middlewares/gateway_auth.h"

#include <stdio.h>
#include <string.h>

static int64_t
im_instance_parse_id(csilk_ctx_t* c)
{
    return parse_query_int_strict(csilk_get_param(c, "id"), -1);
}

static const char*
im_instance_json_string(csilk_json_t* body, const char* key)
{
    if (body == NULL || key == NULL) {
        return NULL;
    }
    return csilk_json_get_string(body, key);
}

static void
im_instance_respond_error(csilk_ctx_t* c, int code, const char* err)
{
    switch (code) {
    case 1001:
        respond_unauthorized(c);
        break;
    case 1002:
        respond_bad_request(c, err ? err : "参数错误");
        break;
    case 1003:
        respond_not_found(c);
        break;
    case 1004:
        respond_conflict(c, err ? err : "操作冲突");
        break;
    default:
        respond_error(c, code > 0 ? code : 1002, err ? err : "操作失败");
        break;
    }
}

static int
im_instance_ok_or_error(csilk_ctx_t* c, int rc, int code, const char* err)
{
    if (rc == 0) {
        respond_ok_null(c);
        return 1;
    }
    im_instance_respond_error(c, code, err);
    return 0;
}

/* Owner-facing routes. */

static void
api_im_instance_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    const char*   platform = im_instance_json_string(body, "platform");
    const char*   adapter = im_instance_json_string(body, "adapter");
    if (platform == NULL) {
        platform = "wechat";
    }
    if (adapter == NULL) {
        adapter = "ilink";
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    out = NULL;
    int              code = 1002;
    char             err[256] = {0};
    int              rc =
        im_instance_usecase_create(pool, user_id, platform, adapter, &out, &code, err, sizeof(err));
    if (body) {
        csilk_json_free(body);
    }
    if (rc != 0) {
        im_instance_respond_error(c, code, err);
        return;
    }
    respond_ok(c, out);
}

static void
api_im_instance_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    csilk_json_t* out = im_instance_usecase_list(db_get_pool(), user_id);
    respond_ok(c, out ? out : csilk_json_array());
}

static void
api_im_instance_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    csilk_json_t* out = im_instance_usecase_get(db_get_pool(), user_id, id);
    if (out == NULL) {
        respond_not_found(c);
        return;
    }
    respond_ok(c, out);
}

static void
api_im_instance_relogin(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    int  code = 1002;
    char err[256] = {0};
    int  rc = im_instance_usecase_relogin(db_get_pool(), user_id, id, &code, err, sizeof(err));
    (void)im_instance_ok_or_error(c, rc, code, err);
}

static void
api_im_instance_verify_code(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    const char*   code_str = im_instance_json_string(body, "code");
    if (code_str == NULL || code_str[0] == '\0') {
        if (body) {
            csilk_json_free(body);
        }
        respond_bad_request(c, "请输入验证码");
        return;
    }
    int  code = 1002;
    char err[256] = {0};
    int  rc = im_instance_usecase_verify_code(
        db_get_pool(), user_id, id, code_str, &code, err, sizeof(err));
    if (body) {
        csilk_json_free(body);
    }
    (void)im_instance_ok_or_error(c, rc, code, err);
}

static void
api_im_instance_set_enabled(csilk_ctx_t* c, int enabled)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    int  code = 1002;
    char err[256] = {0};
    int  rc = im_instance_usecase_set_enabled(
        db_get_pool(), user_id, id, enabled, &code, err, sizeof(err));
    (void)im_instance_ok_or_error(c, rc, code, err);
}

static void
api_im_instance_enable(csilk_ctx_t* c)
{
    api_im_instance_set_enabled(c, 1);
}

static void
api_im_instance_disable(csilk_ctx_t* c)
{
    api_im_instance_set_enabled(c, 0);
}

static void
api_im_instance_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    int  code = 1002;
    char err[256] = {0};
    int  rc = im_instance_usecase_delete(db_get_pool(), user_id, id, &code, err, sizeof(err));
    (void)im_instance_ok_or_error(c, rc, code, err);
}

/* Gateway-facing routes. */

static void
api_gateway_instance_list(csilk_ctx_t* c)
{
    if (!gateway_auth_check(c)) {
        return;
    }
    csilk_json_t* list = im_instance_gateway_list(db_get_pool());
    csilk_json_t* wrap = csilk_json_object();
    csilk_json_add_object(wrap, "instances", list ? list : csilk_json_array());
    respond_ok(c, wrap);
}

static void
api_gateway_instance_get(csilk_ctx_t* c)
{
    if (!gateway_auth_check(c)) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    csilk_json_t* out = im_instance_gateway_get(db_get_pool(), id);
    if (out == NULL) {
        respond_not_found(c);
        return;
    }
    respond_ok(c, out);
}

static void
api_gateway_instance_status(csilk_ctx_t* c)
{
    if (!gateway_auth_check(c)) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    int           code = 1002;
    char          err[256] = {0};
    int           rc = im_instance_gateway_status(db_get_pool(), id, body, &code, err, sizeof(err));
    if (body) {
        csilk_json_free(body);
    }
    (void)im_instance_ok_or_error(c, rc, code, err);
}

static void
api_gateway_instance_heartbeat(csilk_ctx_t* c)
{
    if (!gateway_auth_check(c)) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    int           code = 1002;
    char          err[256] = {0};
    csilk_json_t* out = NULL;
    int rc = im_instance_gateway_heartbeat(db_get_pool(), id, &out, &code, err, sizeof(err));
    if (rc != 0) {
        im_instance_respond_error(c, code, err);
        return;
    }
    respond_ok(c, out);
}

static void
api_gateway_instance_chat(csilk_ctx_t* c)
{
    if (!gateway_auth_check(c)) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    const char*   im_user_id = im_instance_json_string(body, "im_user_id");
    const char*   message = im_instance_json_string(body, "message");
    if (im_user_id == NULL || im_user_id[0] == '\0' || message == NULL || message[0] == '\0') {
        if (body) {
            csilk_json_free(body);
        }
        respond_bad_request(c, "im_user_id 与 message 不能为空");
        return;
    }

    csilk_json_t* sync_data = NULL;
    int64_t       task_id = 0;
    int64_t       session_id = 0;
    bool          deduplicated = false;
    int           code = 1002;
    char          err[256] = {0};
    int           rc = im_instance_gateway_chat(db_get_pool(),
                                                id,
                                                im_user_id,
                                                message,
                                                &sync_data,
                                                &task_id,
                                                &deduplicated,
                                                &session_id,
                                                &code,
                                                err,
                                                sizeof(err));
    if (body) {
        csilk_json_free(body);
    }
    if (rc != 0) {
        im_instance_respond_error(c, code, err);
        return;
    }
    if (sync_data != NULL) {
        respond_ok(c, sync_data);
        return;
    }
    csilk_json_t* extra = csilk_json_object();
    csilk_json_add_int(extra, "bound", 1);
    csilk_json_add_int(extra, "session_id", session_id);
    mf_async_task_respond_submitted(c, task_id, "im_chat", deduplicated, extra);
}

static void
api_gateway_instance_task(csilk_ctx_t* c)
{
    if (!gateway_auth_check(c)) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    int64_t task_id = parse_query_int_strict(csilk_get_param(c, "task_id"), -1);
    if (task_id <= 0) {
        respond_bad_request(c, "无效的任务 ID");
        return;
    }
    int           code = 1002;
    csilk_json_t* out = im_instance_gateway_task(db_get_pool(), id, task_id, &code);
    if (out == NULL) {
        im_instance_respond_error(c, code, "任务不存在");
        return;
    }
    respond_ok(c, out);
}

void
register_im_bot_instance_routes(csilk_app_t* app)
{
    csilk_app_post(app, "/api/im/instances", api_im_instance_create);
    csilk_app_get(app, "/api/im/instances", api_im_instance_list);
    csilk_app_get(app, "/api/im/instances/:id", api_im_instance_get);
    csilk_app_post(app, "/api/im/instances/:id/relogin", api_im_instance_relogin);
    csilk_app_post(app, "/api/im/instances/:id/verify-code", api_im_instance_verify_code);
    csilk_app_post(app, "/api/im/instances/:id/enable", api_im_instance_enable);
    csilk_app_post(app, "/api/im/instances/:id/disable", api_im_instance_disable);
    csilk_app_delete(app, "/api/im/instances/:id", api_im_instance_delete);

    csilk_app_get(app, "/api/gateway/im/instances", api_gateway_instance_list);
    csilk_app_get(app, "/api/gateway/im/instances/:id", api_gateway_instance_get);
    csilk_app_patch(app, "/api/gateway/im/instances/:id/status", api_gateway_instance_status);
    csilk_app_post(app, "/api/gateway/im/instances/:id/heartbeat", api_gateway_instance_heartbeat);
    csilk_app_post(app, "/api/gateway/im/instances/:id/chat", api_gateway_instance_chat);
    csilk_app_get(app, "/api/gateway/im/instances/:id/tasks/:task_id", api_gateway_instance_task);
}
