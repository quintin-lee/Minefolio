/**
 * @file im_bot_controller.c
 * @brief IM 机器人 (QQ / 微信个人号) 只读查账 HTTP 接口层。
 */
#include "interfaces/http/controllers/im_bot_controller.h"
#include "application/ai/im_bot_usecases.h"
#include "application/task/async_task_manager.h"
#include "common/ctx.h"
#include "common/response.h"
#include "common/db.h"
#include <csilk/csilk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IM_PLATFORM_QQ "qq"
#define IM_PLATFORM_WECHAT "wechat"

static int
im_platform_valid(const char* p)
{
    return p && (strcmp(p, IM_PLATFORM_QQ) == 0 || strcmp(p, IM_PLATFORM_WECHAT) == 0);
}

/**
 * @brief 取 platform 并复制到栈 (防止后续 csilk_bind_json 覆写 csilk 内部参数缓冲)。
 */
static int
im_platform_param_safe(csilk_ctx_t* c, char* out, size_t sz)
{
    const char* p = csilk_get_param(c, "platform");
    if (p) {
        snprintf(out, sz, "%s", p);
    }
    if (im_platform_valid(out)) {
        return 1;
    }
    out[0] = '\0';
    return 0;
}

static void
api_im_bindings_token(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    char* bind_token = im_bot_usecase_generate_bind_token(db_get_pool(), c, user_id);
    if (!bind_token) {
        respond_error(c, 500, "生成绑定码失败");
        return;
    }
    csilk_json_t* data = csilk_json_object();
    csilk_json_add_string(data, "bind_token", bind_token);
    csilk_json_add_int(data, "ttl_sec", 300);
    free(bind_token);
    respond_ok(c, data);
}

static void
api_im_bindings_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    int64_t page = 1;
    int64_t page_size = 20;
    parse_page_params(c, &page, &page_size);

    int64_t       total = 0;
    csilk_json_t* list =
        im_bot_usecase_list_bindings_paged(db_get_pool(), user_id, page, page_size, &total);
    respond_page_ok(c, list ? list : csilk_json_array(), total, page, page_size);
}

static void
api_im_bindings_unbind(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    const char*   platform = csilk_json_get_string(body, "platform");
    const char*   im_user_id = csilk_json_get_string(body, "im_user_id");
    if (!im_platform_valid(platform) || !im_user_id || im_user_id[0] == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少合法的 platform 或 im_user_id");
        return;
    }
    im_bot_usecase_unbind(db_get_pool(), platform, im_user_id, user_id);
    csilk_json_free(body);
    respond_ok_null(c);
}

static void
api_im_bind(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    char platform_buf[16];
    if (!im_platform_param_safe(c, platform_buf, sizeof(platform_buf))) {
        respond_bad_request(c, "未知的 IM 平台");
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    const char*   im_user_id = csilk_json_get_string(body, "im_user_id");
    if (!im_user_id || im_user_id[0] == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 im_user_id");
        return;
    }

    csilk_json_t* data = NULL;
    int           code = 0;
    char          err_buf[256] = {0};
    int           rc = im_bot_usecase_bind(db_get_pool(),
                                           c,
                                           user_id,
                                           platform_buf,
                                           im_user_id,
                                           &data,
                                           &code,
                                           err_buf,
                                           sizeof(err_buf));
    csilk_json_free(body);
    if (rc != 0 || !data) {
        respond_error(c, code ? code : 500, err_buf[0] ? err_buf : "绑定失败");
        return;
    }
    respond_ok(c, data);
}

static void
api_im_task_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    int64_t task_id = parse_query_int_strict(csilk_get_param(c, "id"), -1);
    if (task_id <= 0) {
        respond_bad_request(c, "无效的任务 ID");
        return;
    }

    csilk_json_t* task = im_bot_usecase_get_chat_task(db_get_pool(), user_id, task_id);
    if (!task) {
        respond_not_found(c);
        return;
    }
    respond_ok(c, task);
}

static void
api_im_chat(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }
    char platform_buf[16];
    if (!im_platform_param_safe(c, platform_buf, sizeof(platform_buf))) {
        respond_bad_request(c, "未知的 IM 平台");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }
    const char* im_user_id = csilk_json_get_string(body, "im_user_id");
    const char* message = csilk_json_get_string(body, "message");
    if (!im_user_id || im_user_id[0] == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 im_user_id");
        return;
    }
    if (!message || message[0] == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "message 不能为空");
        return;
    }

    csilk_json_t* sync_data = NULL;
    int64_t       task_id = 0;
    bool          deduplicated = false;
    int64_t       session_id = 0;
    int           code = 0;
    char          err_buf[256] = {0};

    int rc = im_bot_usecase_chat_submit(db_get_pool(),
                                        user_id,
                                        platform_buf,
                                        im_user_id,
                                        message,
                                        NULL,
                                        &sync_data,
                                        &task_id,
                                        &deduplicated,
                                        &session_id,
                                        &code,
                                        err_buf,
                                        sizeof(err_buf));
    csilk_json_free(body);

    if (rc != 0) {
        respond_error(c, code ? code : 500, err_buf[0] ? err_buf : "提交异步 AI 查账任务失败");
        return;
    }
    if (sync_data) {
        respond_ok(c, sync_data);
        return;
    }

    csilk_json_t* extra = csilk_json_object();
    csilk_json_add_int(extra, "bound", 1);
    csilk_json_add_int(extra, "session_id", session_id);
    mf_async_task_respond_submitted(c, task_id, "im_chat", deduplicated, extra);
}

static void
api_im_bind_auth(csilk_ctx_t* c)
{
    char platform_buf[16];
    if (!im_platform_param_safe(c, platform_buf, sizeof(platform_buf))) {
        respond_bad_request(c, "未知的 IM 平台");
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }
    const char* im_user_id = csilk_json_get_string(body, "im_user_id");
    const char* username = csilk_json_get_string(body, "username");
    const char* password = csilk_json_get_string(body, "password");
    if (!im_user_id || im_user_id[0] == 0 || !username || username[0] == 0 || !password ||
        password[0] == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 im_user_id / username / password");
        return;
    }

    csilk_json_t* data = NULL;
    int           code = 0;
    char          err_buf[256] = {0};
    int           rc = im_bot_usecase_bind_auth(db_get_pool(),
                                                c,
                                                platform_buf,
                                                im_user_id,
                                                username,
                                                password,
                                                &data,
                                                &code,
                                                err_buf,
                                                sizeof(err_buf));
    csilk_json_free(body);

    if (rc != 0 || !data) {
        respond_error(c, code ? code : 500, err_buf[0] ? err_buf : "绑定失败");
        return;
    }
    respond_ok(c, data);
}

void
register_im_bot_routes(csilk_app_t* app)
{
    if (!app) {
        return;
    }
    csilk_app_post(app, "/api/im/bindings/token", api_im_bindings_token);
    csilk_app_get(app, "/api/im/bindings", api_im_bindings_list);
    csilk_app_delete(app, "/api/im/bindings", api_im_bindings_unbind);
    csilk_app_post(app, "/api/im/:platform/bind", api_im_bind);
    csilk_app_post(app, "/api/im/:platform/bind-auth", api_im_bind_auth);
    csilk_app_post(app, "/api/im/:platform/chat", api_im_chat);
    csilk_app_get(app, "/api/im/tasks/:id", api_im_task_get);
}
