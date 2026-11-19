/**
 * @file im_bot_controller.c
 * @brief IM 机器人 (QQ / 微信个人号) 只读查账接入端点。
 *
 * 能力收敛：IM 入口仅放行只读工具 (非动账)，写操作引导回 App。
 * 身份锚点：im_bot_bindings 表把 (platform, im_user_id) 映射到 mf_user_id。
 *
 *   POST /api/im/bindings/token   App 侧: 签发 300s 受限绑定码 (网关据此调 /bind /chat)
 *   GET  /api/im/bindings         App 侧: 列出我的 IM 绑定
 *   DELETE /api/im/bindings       App 侧: 解绑某平台账号
 *   POST /api/im/:platform/bind   网关回调: 落库绑定 (user 级 token, upsert 幂等)
 *   POST /api/im/:platform/chat   网关回调: 只读查账 (非流式 ai_runtime_execute)
 */
#include "interfaces/http/controllers/im_bot_controller.h"
#include "domain/auth/repository.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "infrastructure/repositories/ai_session_repo_impl.h"
#include "services/ai/runtime/loop.h"
#include "services/ai/runtime/context.h"
#include "services/ai/runtime/session.h"
#include "services/ai/trace/trace.h"
#include "services/ai/tools/registry.h"
#include "services/ai/memory/memory.h"
#include "application/ai/config_provider.h"
#include "common/ctx.h"
#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
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
 * @return 1 合法 (out 已填), 0 非法 (out 填 "")
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

/**
 * @brief App 侧: 签发 300s 受限绑定码。
 * 网关拿该 token 作为 Bearer 调 /bind 与 /chat; 后端 ctx_user_id 即 mf_user_id。
 */
static void
api_im_bindings_token(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }

    /* 取当前 token_version 以便绑定码与用户会话版本一致 */
    int       token_version = 0;
    mf_user_t user = {0};
    if (mf_auth_repo_get_by_id(db_get_pool(), user_id, &user) == 0) {
        token_version = (int)user.token_version;
    }

    char* bind_token = jwt_generate_im_bind_token(c, user_id, token_version, 300);
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

/**
 * @brief App 侧: 列出当前用户的全部 IM 绑定
 */
static void
api_im_bindings_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }
    csilk_json_t* list = mf_im_bot_repo_list_for_user(db_get_pool(), user_id);
    if (!list) {
        list = csilk_json_array();
    }
    respond_ok(c, list);
}

/**
 * @brief App 侧: 解绑 (platform, im_user_id)
 */
static void
api_im_bindings_unbind(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
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
    mf_im_bot_repo_unbind(db_get_pool(), platform, im_user_id, user_id);
    csilk_json_free(body);
    respond_ok_null(c);
}

/**
 * @brief 网关回调: 落库绑定 (user 级 token)。
 * 入参: { im_user_id }。upsert 幂等: 同 (platform, im_user_id) 多次 bind 结果一致,
 * 身份由 JWT sub 决定, 无法伪造他人。
 */
static void
api_im_bind(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }
    /* platform 在 csilk_bind_json 前 snapshot 到栈, 避免内部参数缓冲被 body 解析覆写 */
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

    /* 防抢占: 该 im_user_id 若已绑定他人, 拒绝 — 不可用自己的绑定码改写他人绑定 */
    char    pre_scope[32] = "readonly";
    int64_t pre_bound = mf_im_bot_repo_lookup(
        db_get_pool(), platform_buf, im_user_id, pre_scope, sizeof(pre_scope));
    if (pre_bound > 0 && pre_bound != user_id) {
        csilk_json_free(body);
        respond_error(c, 1004, "该 IM 账号已被其他用户绑定");
        return;
    }

    int rc = mf_im_bot_repo_upsert(db_get_pool(), platform_buf, im_user_id, user_id, "readonly");
    /* im_user_id 是 body 的借用指针, free 前复制到栈 */
    char im_user_id_buf[128];
    snprintf(im_user_id_buf, sizeof(im_user_id_buf), "%s", im_user_id);
    csilk_json_free(body);
    if (rc != 0) {
        respond_error(c, 500, "绑定失败");
        return;
    }
    csilk_json_t* data = csilk_json_object();
    csilk_json_add_string(data, "platform", platform_buf);
    csilk_json_add_string(data, "im_user_id", im_user_id_buf);
    csilk_json_add_string(data, "scope", "readonly");
    csilk_json_add_string(data, "message", "绑定成功，现在可以问我持仓/汇率了");
    respond_ok(c, data);
}

/**
 * @brief 网关回调: 只读查账 (非流式)。
 * 入参: { im_user_id, message, session_key? }
 * 流程:
 *   1. ctx_user_id (网关已注入 user 级 token) → mf_user_id
 *   2. 校验 im_bot_bindings (platform, im_user_id) 存在 → 能力锚点
 *   3. 会话映射: 复用 (user, "IM: <platform>@<im_user_id>") 会话; 无则建
 *   4. 非流式 ai_runtime_execute + 只读工具白名单 (ai_tool_get_readonly_csilk_definitions)
 *   5. 落库 user/assistant 消息 → 回 { reply }
 */
static void
api_im_chat(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }
    /* platform 在 csilk_bind_json 前 snapshot 到栈, 避免内部参数缓冲被 body 解析覆写 */
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

    /* 能力锚点: 无绑定 → 拒绝查询, 引导绑定。
     * 一致性校验: 该 im_user_id 绑定的 mf_user_id 必须等于当前 JWT 身份 (ctx_user_id),
     * 否则拒绝 — 防止一个绑定码被未绑定的他人借用, 污染他人 IM 会话。 */
    char    scope[32] = "readonly";
    int64_t bound =
        mf_im_bot_repo_lookup(db_get_pool(), platform_buf, im_user_id, scope, sizeof(scope));
    if (bound <= 0 || bound != user_id) {
        csilk_json_free(body);
        csilk_json_t* data = csilk_json_object();
        csilk_json_add_string(
            data,
            "reply",
            "尚未绑定。请先到 App「IM 机器人」生成绑定码，发送 /bind <码> 完成绑定。");
        csilk_json_add_int(data, "bound", 0);
        respond_ok(c, data);
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    /* 会话映射: 固定标题定位该 IM 用户的会话 (与 App 数据统一) */
    char sess_title[128];
    snprintf(sess_title, sizeof(sess_title), "IM: %s@%s", platform_buf, im_user_id);
    int64_t       list_total = 0;
    csilk_json_t* found = mf_ai_session_list(pool, user_id, 1, 50, &list_total);
    (void)list_total; /* 会话列表仅按 title 定位, 不用 total */
    int64_t session_id = 0;
    if (found) {
        for (size_t i = 0; i < csilk_json_array_size(found); i++) {
            csilk_json_t* row = csilk_json_array_get(found, i);
            const char*   t = csilk_json_get_string(row, "title");
            if (t && strcmp(t, sess_title) == 0) {
                session_id = db_get_int(row, "id");
                break;
            }
        }
        csilk_json_free(found);
    }
    if (session_id <= 0) {
        session_id = mf_ai_session_insert(pool, user_id, sess_title, "im-bot", "system");
    }
    if (session_id <= 0) {
        csilk_json_free(body);
        respond_error(c, 500, "创建会话失败");
        return;
    }

    /* 落库 user 消息 */
    mf_ai_message_insert(pool, session_id, "user", message, "im-bot", NULL, NULL);

    /* 构造非流式 runtime 上下文: 只读工具白名单 + 会话历史 */
    ai_config_t* cfg = ai_config_acquire();
    const char*  model_sel = (cfg && cfg->default_model[0]) ? cfg->default_model : "default";

    ai_runtime_context_t rctx;
    ai_runtime_context_init(&rctx);
    rctx.user_id = user_id;
    rctx.session_id = session_id;
    snprintf(rctx.model_name, sizeof(rctx.model_name), "%s", model_sel);
    rctx.temperature = 0.5;
    rctx.max_tokens = 1024;
    rctx.limits.max_iterations = 4;
    rctx.limits.token_budget = 8192;

    /* 只读工具: 过滤 is_mutation, LLM 无法发起写操作 */
    size_t                 ro_count = 0;
    const csilk_ai_tool_t* ro_tools = ai_tool_get_readonly_csilk_definitions(&ro_count);
    rctx.tools = (csilk_ai_tool_t*)ro_tools;
    rctx.tool_count = ro_count;

    /* 会话历史 → 受控消息窗口 */
    int           ctx_size = (cfg && cfg->context_size > 0) ? cfg->context_size : 20;
    csilk_json_t* history = mf_ai_message_recent(pool, session_id, ctx_size);
    csilk_json_free(rctx.messages);
    rctx.messages =
        ai_memory_build_messages(cfg ? cfg->system_prompt : NULL, history, message, ctx_size, NULL);

    csilk_json_free(history);

    /* 非流式执行 */
    ai_trace_t*         trace = NULL;
    ai_runtime_result_t res = ai_runtime_execute(pool, &rctx, &trace);
    ai_runtime_context_free(&rctx);

    /* 持久化 assistant 消息 + trace */
    char* final_text = res.final_content ? res.final_content : "";
    mf_ai_message_insert(pool, session_id, "assistant", final_text, model_sel, NULL, NULL);
    if (trace) {
        if (session_id > 0) {
            mf_ai_trace_save(pool, trace);
        }
        ai_trace_destroy(trace);
        trace = NULL;
    }

    csilk_json_t* data = csilk_json_object();
    csilk_json_add_string(data, "reply", final_text ? final_text : "");
    csilk_json_add_int(data, "bound", 1);
    if (res.status.code != 0) {
        /* 模型/运行时错误: 回友好提示而非崩溃 */
        csilk_json_free(data);
        data = csilk_json_object();
        csilk_json_add_string(data, "reply", "AI 查询失败，请稍后再试。");
    }
    free(res.final_content);
    csilk_json_free(body);

    if (cfg) {
        ai_config_release(cfg);
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
    csilk_app_post(app, "/api/im/:platform/chat", api_im_chat);
}
