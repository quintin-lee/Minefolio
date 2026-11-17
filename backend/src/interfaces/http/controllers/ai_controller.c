#include "interfaces/http/controllers/ai_controller.h"
#include "application/ai/usecases.h"
#include "application/ai/probe_usecase.h"
#include "services/ai_service.h"
#include "services/ai_workflow_service.h"
#include "infrastructure/repositories/auth_repo_impl.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/registry.h"
#include "services/ai/tools/context.h"
#include "services/ai/tools/batch_tool.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 实例管理员门禁：AI 全局配置为实例级共享资源，仅管理员（最早注册的账号）可改写/探测
 *
 * 读接口 GET /api/settings/ai 对全体登录用户保持开放：聊天工作台（默认模型）与票据
 * OCR 依赖该接口，且响应不含密钥明文（仅 has_api_key 布尔位）。
 *
 * 注：base_url 允许指向内网/本机——自托管场景下 Ollama 等推理服务常部署于局域网，
 * 属管理员预期配置，因此不套用 mf_ai_rule_validate_url 的内网阻断；探测端点的风险
 * 面由「仅实例管理员可达」收敛。
 */
static int
require_instance_admin(csilk_ctx_t* c, int64_t user_id)
{
    if (mf_auth_repo_is_instance_admin(db_get_pool(), user_id)) {
        return 1;
    }
    respond_error(c, 1004, "仅实例管理员可管理 AI 配置");
    return 0;
}

void
api_ai_models_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

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
    int64_t     id = parse_query_int_strict(id_str, -1);
    if (id <= 0) {
        respond_bad_request(c, "缺少或无效的 id");
        return;
    }

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
    int64_t     id = parse_query_int_strict(id_str, -1);
    if (id <= 0) {
        respond_bad_request(c, "缺少或无效的 id");
        return;
    }

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
    int64_t     id = parse_query_int_strict(id_str, -1);
    if (id <= 0) {
        respond_bad_request(c, "缺少或无效的 id");
        return;
    }

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
    int64_t     session_id = parse_query_int_strict(id_str, -1);
    if (session_id <= 0) {
        respond_bad_request(c, "缺少或无效的 session_id");
        return;
    }

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

static char*
ai_str_replace_first(const char* src, const char* target, const char* replacement)
{
    if (!src || !target || !replacement) {
        return NULL;
    }
    const char* pos = strstr(src, target);
    if (!pos) {
        return NULL;
    }
    size_t prefix_len = (size_t)(pos - src);
    size_t orig_len = strlen(src);
    size_t target_len = strlen(target);
    size_t repl_len = strlen(replacement);
    size_t new_len = orig_len + repl_len - target_len + 1;
    char*  out = (char*)malloc(new_len);
    if (!out) {
        return NULL;
    }
    memcpy(out, src, prefix_len);
    memcpy(out + prefix_len, replacement, repl_len);
    strcpy(out + prefix_len + repl_len, pos + target_len);
    return out;
}

static char*
ai_mark_content_executed(const char* content, const char* draft_token)
{
    if (!content || !content[0] || !draft_token || !draft_token[0]) {
        return NULL;
    }
    if (!strstr(content, draft_token)) {
        return NULL;
    }
    if (strstr(content, "\"status\":\"executed\"") || strstr(content, "\"status\": \"executed\"")) {
        return NULL;
    }
    const char* target = NULL;
    const char* replacement = NULL;
    if (strstr(content, "\"status\": \"pending\"")) {
        target = "\"status\": \"pending\"";
        replacement = "\"status\": \"executed\"";
    } else if (strstr(content, "\"status\":\"pending\"")) {
        target = "\"status\":\"pending\"";
        replacement = "\"status\":\"executed\"";
    } else if (strstr(content, "\"status\": \"proposed\"")) {
        target = "\"status\": \"proposed\"";
        replacement = "\"status\": \"executed\"";
    } else if (strstr(content, "\"status\":\"proposed\"")) {
        target = "\"status\":\"proposed\"";
        replacement = "\"status\":\"executed\"";
    } else if (strstr(content, "\"action_type\"")) {
        target = "\"action_type\"";
        replacement = "\"status\":\"executed\",\"action_type\"";
    }
    if (target && replacement) {
        return ai_str_replace_first(content, target, replacement);
    }
    return NULL;
}

static char*
ai_mark_tool_calls_executed(const char* tool_calls, const char* draft_token)
{
    if (!tool_calls || !tool_calls[0] || !draft_token || !draft_token[0]) {
        return NULL;
    }
    if (!strstr(tool_calls, draft_token)) {
        return NULL;
    }
    if (strstr(tool_calls, "\\\"status\\\":\\\"executed\\\"") ||
        strstr(tool_calls, "\\\"status\\\": \\\"executed\\\"") ||
        strstr(tool_calls, "\"status\":\"executed\"") ||
        strstr(tool_calls, "\"status\": \"executed\"")) {
        return NULL;
    }
    const char* target = NULL;
    const char* replacement = NULL;
    if (strstr(tool_calls, "\\\"status\\\": \\\"pending\\\"")) {
        target = "\\\"status\\\": \\\"pending\\\"";
        replacement = "\\\"status\\\": \\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"status\\\":\\\"pending\\\"")) {
        target = "\\\"status\\\":\\\"pending\\\"";
        replacement = "\\\"status\\\":\\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"status\\\": \\\"proposed\\\"")) {
        target = "\\\"status\\\": \\\"proposed\\\"";
        replacement = "\\\"status\\\": \\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"status\\\":\\\"proposed\\\"")) {
        target = "\\\"status\\\":\\\"proposed\\\"";
        replacement = "\\\"status\\\":\\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"propose_success\\\":true")) {
        target = "\\\"propose_success\\\":true";
        replacement = "\\\"propose_success\\\":true,\\\"status\\\":\\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"propose_success\\\": true")) {
        target = "\\\"propose_success\\\": true";
        replacement = "\\\"propose_success\\\": true,\\\"status\\\": \\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"action_type\\\"")) {
        target = "\\\"action_type\\\"";
        replacement = "\\\"status\\\":\\\"executed\\\",\\\"action_type\\\"";
    } else if (strstr(tool_calls, "\"status\": \"pending\"")) {
        target = "\"status\": \"pending\"";
        replacement = "\"status\": \"executed\"";
    } else if (strstr(tool_calls, "\"status\":\"pending\"")) {
        target = "\"status\":\"pending\"";
        replacement = "\"status\":\"executed\"";
    } else if (strstr(tool_calls, "\"action_type\"")) {
        target = "\"action_type\"";
        replacement = "\"status\":\"executed\",\"action_type\"";
    }
    if (target && replacement) {
        return ai_str_replace_first(tool_calls, target, replacement);
    }
    return NULL;
}

void
ai_message_mark_action_executed(csilk_db_pool_t* pool, int64_t user_id, const char* draft_token)
{
    if (!pool || user_id <= 0 || !draft_token || !draft_token[0]) {
        return;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%%%s%%", draft_token);

    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT id, content, tool_calls FROM ai_messages "
        "WHERE session_id IN (SELECT id FROM ai_sessions WHERE user_id = ?) "
        "  AND (content LIKE ? OR tool_calls LIKE ?) "
        "ORDER BY id DESC LIMIT 1",
        (const char*[]){uid_str, pattern, pattern, NULL});

    if (!rows || !csilk_json_is_array(rows) || csilk_json_array_size(rows) == 0) {
        if (rows) {
            csilk_json_free(rows);
        }
        return;
    }

    const csilk_json_t* row = csilk_json_array_get(rows, 0);
    int64_t             msg_id = db_get_int(row, "id");
    const char*         content = csilk_json_get_string(row, "content");
    const char*         tool_calls = csilk_json_get_string(row, "tool_calls");

    if (msg_id > 0) {
        char* new_content = ai_mark_content_executed(content, draft_token);
        char* new_tool_calls = ai_mark_tool_calls_executed(tool_calls, draft_token);

        if (new_content || new_tool_calls) {
            char mid_str[32];
            snprintf(mid_str, sizeof(mid_str), "%lld", (long long)msg_id);
            const char* final_content = new_content ? new_content : (content ? content : "");
            const char* final_tc = new_tool_calls ? new_tool_calls : (tool_calls ? tool_calls : "");

            csilk_json_t* upd = csilk_db_query_param_json(
                pool,
                "UPDATE ai_messages SET content = ?, tool_calls = ? WHERE id = ?",
                (const char*[]){final_content, final_tc, mid_str, NULL});
            if (upd) {
                csilk_json_free(upd);
            }
        }
        if (new_content) {
            free(new_content);
        }
        if (new_tool_calls) {
            free(new_tool_calls);
        }
    }
    csilk_json_free(rows);
}

/**
 * @brief 用户确认执行 AI 拟录入的记账/转账动作
 *
 * 请求体：{action_type: "daily_expense"|"transfer", draft_token, ...confirm 参数}
 * （confirm_proposed_expense: amount/type/date/category_id/asset_id/note；
 *   confirm_proposed_transfer: amount/from_asset_id/to_asset_id/date/note）
 *
 * 动作经统一工具调度（policy + audit + HMAC 令牌验签）执行：
 * - 令牌由 propose_* 工具在用户会话内签发（300s TTL、绑定参数哈希）；
 * - 参数与令牌不符 / 令牌过期 / 重放 → 拒绝，不落账。
 */
void
api_ai_action_confirm_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char* action_type = csilk_json_get_string(body, "action_type");
    const char* draft_token = csilk_json_get_string(body, "draft_token");
    if (!action_type || !action_type[0] || !draft_token || !draft_token[0]) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 action_type 或 draft_token");
        return;
    }

    char token_buf[512] = {0};
    strncpy(token_buf, draft_token, sizeof(token_buf) - 1);

    csilk_db_pool_t* pool = db_get_pool();
    const char*      tool_name = NULL;
    if (strcmp(action_type, "daily_expense") == 0) {
        tool_name = "confirm_proposed_expense";
    } else if (strcmp(action_type, "transfer") == 0) {
        tool_name = "confirm_proposed_transfer";
    } else {
        csilk_json_free(body);
        respond_bad_request(c, "未知的 action_type");
        return;
    }

    /* 透传 confirm 参数（剔除 action_type）并强制回填 draft_token，
       防止模型伪造参数绕过令牌绑定 */
    csilk_json_t*      args = csilk_json_object();
    ai_tool_context_t* ctx = ai_tool_context_create(pool, user_id, 0 /* 无会话上下文 */, NULL);
    if (!ctx) {
        csilk_json_free(body);
        csilk_json_free(args);
        respond_error(c, 500, "确认执行失败");
        return;
    }
    size_t bsize = csilk_json_object_size(body);
    for (size_t i = 0; i < bsize; i++) {
        const char*         key = csilk_json_object_key(body, i);
        const csilk_json_t* val = csilk_json_object_val(body, i);
        if (!key || strcmp(key, "action_type") == 0 || strcmp(key, "draft_token") == 0) {
            continue;
        }
        csilk_json_add_object(args, key, csilk_json_copy((csilk_json_t*)val));
    }
    csilk_json_add_string(args, "draft_token", token_buf);
    csilk_json_free(body);

    char* result = ai_tool_dispatch_parsed(ctx, tool_name, args);
    ai_tool_context_free(ctx);
    csilk_json_free(args);
    if (!result) {
        respond_error(c, 500, "确认执行失败");
        return;
    }

    /* 工具结果为 {success:true,...} 或 {error:...}；原样透传给前端 */
    csilk_json_t* parsed = csilk_json_parse(result);
    if (parsed) {
        if (csilk_json_get_bool(parsed, "success")) {
            ai_message_mark_action_executed(pool, user_id, token_buf);
        }
        respond_ok(c, parsed);
    } else {
        respond_ok_null(c);
    }
    free(result);
}

void
api_ai_action_confirm_batch_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char* draft_token = csilk_json_get_string(body, "draft_token");
    if (!draft_token || !draft_token[0]) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 draft_token");
        return;
    }

    char token_buf[512] = {0};
    strncpy(token_buf, draft_token, sizeof(token_buf) - 1);

    const csilk_json_t* actions = csilk_json_get(body, "actions");
    if (!actions || !csilk_json_is_array(actions) || csilk_json_array_size(actions) == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 actions 数组");
        return;
    }

    csilk_json_t* out_data = NULL;
    int           code = 0;
    char          err_buf[256] = {0};
    int           rc = ai_batch_actions_execute(
        db_get_pool(), user_id, actions, token_buf, &out_data, &code, err_buf, sizeof(err_buf));
    csilk_json_free(body);

    if (rc == 0 && out_data) {
        ai_message_mark_action_executed(db_get_pool(), user_id, token_buf);
        respond_ok(c, out_data);
    } else {
        if (out_data) {
            csilk_json_free(out_data);
        }
        if (code == 1004) {
            respond_error(c, 1004, err_buf[0] ? err_buf : "操作冲突或令牌已被使用");
        } else if (code == 1002) {
            respond_bad_request(c, err_buf[0] ? err_buf : "参数错误或令牌篡改");
        } else {
            respond_error(c, 500, err_buf[0] ? err_buf : "批量执行失败");
        }
    }
}

void
api_ai_settings_get_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

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
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    if (!require_instance_admin(c, user_id)) {
        return;
    }

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
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    if (!require_instance_admin(c, user_id)) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    csilk_json_t*       out_resp = NULL;
    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_test_connection(body, &out_resp, &res);
    csilk_json_free(body);

    if (rc == 0 && out_resp) {
        respond_ok(c, out_resp);
    } else {
        if (out_resp) {
            csilk_json_free(out_resp);
        }
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_fetch_models_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    if (!require_instance_admin(c, user_id)) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    csilk_json_t*       out_resp = NULL;
    ai_usecase_result_t res = {0};
    int                 rc = ai_usecase_fetch_models(body, &out_resp, &res);
    csilk_json_free(body);

    if (rc == 0 && out_resp) {
        respond_ok(c, out_resp);
    } else {
        if (out_resp) {
            csilk_json_free(out_resp);
        }
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_ai_workflows_list_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

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
    csilk_app_post_ext(app,
                       "/api/ai/actions/confirm",
                       api_ai_action_confirm_handler,
                       NULL,
                       NULL,
                       "Confirm AI proposed action",
                       "Verify the draft token and execute the confirmed ledger write");
    csilk_app_post_ext(
        app,
        "/api/ai/actions/confirm-batch",
        api_ai_action_confirm_batch_handler,
        NULL,
        NULL,
        "Confirm AI proposed batch actions",
        "Verify the composite batch draft token and execute multi-action ledger writes atomically");
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
