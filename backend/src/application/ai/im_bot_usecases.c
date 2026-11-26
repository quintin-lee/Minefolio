#include "application/ai/im_bot_usecases.h"
#include "application/ai/config_provider.h"
#include "application/auth/usecases.h"
#include "application/task/async_task_manager.h"
#include "domain/auth/repository.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "infrastructure/repositories/ai_session_repo_impl.h"
#include "services/ai/runtime/loop.h"
#include "services/ai/runtime/context.h"
#include "services/ai/runtime/session.h"
#include "services/ai/trace/trace.h"
#include "services/ai/tools/registry.h"
#include "services/ai/memory/memory.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/crypto/bcrypt.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** iLink 记账草案短码捕获上下文（仅 query+confirm 能力档使用）。 */
typedef struct {
    csilk_db_pool_t* pool;
    int64_t          user_id;
    int64_t          session_id;
    char             code[16];
} im_propose_capture_t;

typedef struct {
    int64_t session_id;
    char*   message;
    char    capability[16];
} im_chat_task_arg_t;

static void
im_chat_task_cleanup(void* raw)
{
    im_chat_task_arg_t* arg = (im_chat_task_arg_t*)raw;
    if (!arg) {
        return;
    }
    free(arg->message);
    free(arg);
}

/** 剥离模型可能回显的 draft_token（纵深防御；确认只走独立端点）。 */
static char*
im_strip_draft_tokens(const char* text)
{
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char*  out = (char*)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t j = 0;
    for (size_t i = 0; i < len;) {
        if (strncmp(text + i, "mf_v2.", 6) == 0) {
            i += 6;
            while (i < len && !isspace((unsigned char)text[i])) {
                i++;
            }
            continue;
        }
        out[j++] = text[i++];
    }
    out[j] = '\0';
    return out;
}

/** 生成 8 位大写短码（排除易混淆的 O/0/I/1）。 */
static int
im_generate_confirm_code(char* out, size_t cap)
{
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    const size_t      alen = sizeof(alphabet) - 1;
    unsigned char     rnd[8];
    if (!out || cap < 9 || RAND_bytes(rnd, (int)sizeof(rnd)) != 1) {
        return -1;
    }
    for (size_t i = 0; i < 8; i++) {
        out[i] = alphabet[rnd[i] % alen];
    }
    out[8] = '\0';
    OPENSSL_cleanse(rnd, sizeof(rnd));
    return 0;
}

/** 捕获 propose_daily_expense 的工具结果，落短码草案。 */
static void
im_on_tool_result(const char* id, const char* name, const char* result_json, void* udata)
{
    (void)id;
    im_propose_capture_t* cap = (im_propose_capture_t*)udata;
    if (!cap || !cap->pool || !name || strcmp(name, "propose_daily_expense") != 0 || !result_json ||
        cap->code[0]) {
        return;
    }
    csilk_json_t* res = csilk_json_parse(result_json);
    if (!res) {
        return;
    }
    const char* draft = csilk_json_get_string(res, "draft_token");
    if (!draft || !draft[0]) {
        csilk_json_free(res);
        return;
    }
    /* 优先使用工具回传的原始参数快照，避免结果脱敏/量化造成的参数哈希漂移。 */
    const char* raw_args = csilk_json_get_string(res, "draft_args_json");
    char*       args_json = NULL;
    if (raw_args && raw_args[0]) {
        args_json = strdup(raw_args);
    } else {
        const char*   type = csilk_json_get_string(res, "type");
        const char*   date = csilk_json_get_string(res, "date");
        const char*   note = csilk_json_get_string(res, "note");
        double        amount = csilk_json_get_number(res, "amount");
        int64_t       cat_id = (int64_t)csilk_json_get_number(res, "category_id");
        int64_t       asset_id = (int64_t)csilk_json_get_number(res, "asset_id");
        csilk_json_t* args = csilk_json_object();
        csilk_json_add_string(args, "type", (type && type[0]) ? type : "expense");
        csilk_json_add_number(args, "amount", amount);
        csilk_json_add_string(args, "date", date ? date : "");
        csilk_json_add_number(args, "category_id", (double)cat_id);
        csilk_json_add_number(args, "asset_id", (double)asset_id);
        csilk_json_add_string(args, "note", note ? note : "");
        args_json = csilk_json_serialize(args, NULL);
        csilk_json_free(args);
    }
    csilk_json_free(res);
    if (!args_json) {
        return;
    }

    char code[9];
    if (im_generate_confirm_code(code, sizeof(code)) != 0) {
        free(args_json);
        return;
    }
    int64_t now = (int64_t)time(NULL);
    if (mf_im_bot_pending_action_insert(cap->pool,
                                        code,
                                        cap->user_id,
                                        cap->session_id,
                                        "daily_expense",
                                        draft,
                                        args_json,
                                        now + 300) == 0) {
        snprintf(cap->code, sizeof(cap->code), "%s", code);
    }
    free(args_json);
}

static int
im_chat_task_fn(void*   pool,
                int64_t task_id,
                int64_t user_id,
                int64_t ledger_id,
                void*   arg,
                char**  out_result_json,
                char*   err_buf,
                size_t  err_cap)
{
    (void)ledger_id;
    im_chat_task_arg_t* targ = (im_chat_task_arg_t*)arg;
    if (!targ || targ->session_id <= 0 || !targ->message) {
        snprintf(err_buf, err_cap, "参数错误");
        return -1;
    }

    int can_book = (strcmp(targ->capability, "query+confirm") == 0);
    mf_async_task_update_progress(
        pool, task_id, 20, can_book ? "正在执行 AI 记账助手" : "正在执行只读 AI 查账");

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    ai_config_t*     cfg = ai_config_acquire();
    const char*      model_sel = (cfg && cfg->default_model[0]) ? cfg->default_model : "default";

    ai_runtime_context_t rctx;
    ai_runtime_context_init(&rctx);
    rctx.user_id = user_id;
    rctx.session_id = targ->session_id;
    snprintf(rctx.model_name, sizeof(rctx.model_name), "%s", model_sel);
    rctx.temperature = 0.5;
    rctx.max_tokens = 1024;
    rctx.limits.max_iterations = 4;
    rctx.limits.token_budget = 8192;

    /* 工具白名单：只读集恒定可用；query+confirm 仅额外放行 propose_daily_expense。
       confirm_* 为 internal_only，这里永远不会暴露给模型。 */
    csilk_ai_tool_t        tools[64];
    size_t                 ntools = 0;
    size_t                 ro_count = 0;
    const csilk_ai_tool_t* ro_tools = ai_tool_get_readonly_csilk_definitions(&ro_count);
    for (size_t i = 0; i < ro_count && ntools < 64; i++) {
        tools[ntools++] = ro_tools[i];
    }
    if (can_book) {
        static const char* const write_names[] = {"propose_daily_expense"};
        ntools += ai_tool_get_csilk_definitions_subset(write_names, 1, tools + ntools, 64 - ntools);
    }
    rctx.tools = tools;
    rctx.tool_count = ntools;

    int           ctx_size = (cfg && cfg->context_size > 0) ? cfg->context_size : 20;
    csilk_json_t* history = mf_ai_message_recent(db_pool, targ->session_id, ctx_size);
    csilk_json_free(rctx.messages);
    rctx.messages = ai_memory_build_messages(
        cfg ? cfg->system_prompt : NULL, history, targ->message, ctx_size, NULL);
    csilk_json_free(history);

    if (can_book) {
        csilk_json_t* ch = csilk_json_object();
        csilk_json_add_string(ch, "role", "system");
        csilk_json_add_string(
            ch,
            "content",
            "当前为微信 iLink 记账通道。用户要求记账时只能调用 propose_daily_expense 生成草案，"
            "并用中文简述金额、分类与支付账户；不得要求用户提供 draft_token，不得自行确认或"
            "声称已记账。系统会自动发送确认码，用户回复后才会真正落账。");
        csilk_json_array_append(rctx.messages, ch);
    }

    im_propose_capture_t   capture = {db_pool, user_id, targ->session_id, ""};
    ai_runtime_callbacks_t cbs = {0};
    cbs.on_tool_result = im_on_tool_result;
    ai_trace_t*         trace = NULL;
    ai_runtime_result_t res =
        ai_runtime_execute_with_callbacks(db_pool, &rctx, &cbs, &capture, &trace);
    ai_runtime_context_free(&rctx);

    const char* final_text = res.final_content ? res.final_content : "";
    char*       clean = im_strip_draft_tokens(final_text);
    char*       reply = clean ? clean : strdup("");
    if (capture.code[0]) {
        size_t need = strlen(reply) + 160;
        char*  with_code = (char*)malloc(need);
        if (with_code) {
            snprintf(with_code,
                     need,
                     "%s\n\n回复「确认 %s」完成记账（5 分钟内有效，仅你本人微信可用）。",
                     reply,
                     capture.code);
            free(reply);
            reply = with_code;
        }
    }
    mf_ai_message_insert(
        db_pool, targ->session_id, "assistant", reply ? reply : "", model_sel, NULL, NULL);
    if (trace) {
        if (targ->session_id > 0) {
            mf_ai_trace_save(db_pool, trace);
        }
        ai_trace_destroy(trace);
        trace = NULL;
    }

    csilk_json_t* data = csilk_json_object();
    if (res.status.code != 0) {
        csilk_json_add_string(data, "reply", "AI 查询失败，请稍后再试。");
    } else {
        csilk_json_add_string(data, "reply", reply ? reply : "");
    }
    csilk_json_add_int(data, "bound", 1);
    csilk_json_add_int(data, "session_id", targ->session_id);

    if (out_result_json) {
        *out_result_json = csilk_json_serialize(data, NULL);
    }
    csilk_json_free(data);
    free(reply);
    free(res.final_content);

    if (cfg) {
        ai_config_release(cfg);
    }
    return 0;
}

char*
im_bot_usecase_generate_bind_token(void* pool, csilk_ctx_t* c, int64_t user_id)
{
    int       token_version = 0;
    mf_user_t user = {0};
    if (pool && mf_auth_repo_get_by_id((csilk_db_pool_t*)pool, user_id, &user) == 0) {
        token_version = (int)user.token_version;
    }
    return jwt_generate_im_bind_token(c, user_id, token_version, 300);
}

csilk_json_t*
im_bot_usecase_list_bindings(void* pool, int64_t user_id)
{
    if (!pool || user_id < 0) {
        return csilk_json_array();
    }
    csilk_json_t* list = mf_im_bot_repo_list_for_user((csilk_db_pool_t*)pool, user_id);
    return list ? list : csilk_json_array();
}

int
im_bot_usecase_unbind(void* pool, const char* platform, const char* im_user_id, int64_t user_id)
{
    if (!pool || !platform || !im_user_id || user_id < 0) {
        return -1;
    }
    return mf_im_bot_repo_unbind((csilk_db_pool_t*)pool, platform, im_user_id, user_id);
}

int
im_bot_usecase_bind(void*          pool,
                    csilk_ctx_t*   c,
                    int64_t        user_id,
                    const char*    platform,
                    const char*    im_user_id,
                    csilk_json_t** out_data,
                    int*           out_code,
                    char*          err_buf,
                    size_t         err_cap)
{
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    char             pre_scope[32] = "readonly";
    int64_t          pre_bound =
        mf_im_bot_repo_lookup(db_pool, platform, im_user_id, pre_scope, sizeof(pre_scope));
    if (pre_bound > 0 && pre_bound != user_id) {
        *out_code = 1004;
        snprintf(err_buf, err_cap, "该 IM 账号已被其他用户绑定");
        return -1;
    }

    int rc = mf_im_bot_repo_upsert(db_pool, platform, im_user_id, user_id, "readonly");
    if (rc != 0) {
        *out_code = 500;
        snprintf(err_buf, err_cap, "绑定失败");
        return -1;
    }

    csilk_json_t* data = csilk_json_object();
    csilk_json_add_string(data, "platform", platform);
    csilk_json_add_string(data, "im_user_id", im_user_id);
    csilk_json_add_string(data, "scope", "readonly");
    csilk_json_add_string(data, "message", "绑定成功，现在可以问我持仓/汇率了");

    int       token_version = 0;
    mf_user_t user = {0};
    if (mf_auth_repo_get_by_id(db_pool, user_id, &user) == 0) {
        token_version = (int)user.token_version;
    }
    char* long_token = jwt_generate_im_bind_token(c, user_id, token_version, 365 * 86400);
    if (long_token) {
        csilk_json_add_string(data, "bind_token", long_token);
        free(long_token);
    }

    *out_data = data;
    *out_code = 0;
    return 0;
}

int
im_bot_usecase_bind_auth(void*          pool,
                         csilk_ctx_t*   c,
                         const char*    platform,
                         const char*    im_user_id,
                         const char*    username,
                         const char*    password,
                         csilk_json_t** out_data,
                         int*           out_code,
                         char*          err_buf,
                         size_t         err_cap)
{
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    char             password_buf[128];
    snprintf(password_buf, sizeof(password_buf), "%s", password);

    mf_user_t user;
    memset(&user, 0, sizeof(user));
    int rc = mf_auth_repo_find_by_username(db_pool, username, &user);

    if (rc != 0 || user.id <= 0 || user.password_hash[0] == 0) {
        csilk_bcrypt_verify(password_buf,
                            strlen(password_buf),
                            "$2b$12$abcdefghijklmnopqrstuuABCDEFGHIJKLMNOPQRSTUVWXYZ01234");
        OPENSSL_cleanse(password_buf, sizeof(password_buf));
        *out_code = 1004;
        snprintf(err_buf, err_cap, "用户名或密码错误");
        return -1;
    }
    if (auth_usecase_verify_and_upgrade_password(
            pool, user.id, password_buf, strlen(password_buf), user.password_hash) != 0) {
        OPENSSL_cleanse(password_buf, sizeof(password_buf));
        *out_code = 1004;
        snprintf(err_buf, err_cap, "用户名或密码错误");
        return -1;
    }
    OPENSSL_cleanse(password_buf, sizeof(password_buf));

    int is_2fa_active = (user.totp_secret[0] != '\0') &&
                        (user.totp_enabled || (user.totp_backup_codes[0] != '\0' &&
                                               strcmp(user.totp_backup_codes, "[]") != 0));
    if (is_2fa_active) {
        *out_code = 1004;
        snprintf(err_buf,
                 err_cap,
                 "该账号已启用 2FA 双因素认证，请在 Web 端生成绑定码后使用 /bind <绑定码> 绑定");
        return -1;
    }

    char    pre_scope[32] = "readonly";
    int64_t pre_bound =
        mf_im_bot_repo_lookup(db_pool, platform, im_user_id, pre_scope, sizeof(pre_scope));
    if (pre_bound > 0 && pre_bound != user.id) {
        *out_code = 1004;
        snprintf(err_buf, err_cap, "该 IM 账号已被其他用户绑定");
        return -1;
    }

    int up = mf_im_bot_repo_upsert(db_pool, platform, im_user_id, user.id, "readonly");
    if (up != 0) {
        *out_code = 500;
        snprintf(err_buf, err_cap, "绑定失败");
        return -1;
    }

    char* bind_token = jwt_generate_im_bind_token(c, user.id, (int)user.token_version, 365 * 86400);
    if (!bind_token) {
        *out_code = 500;
        snprintf(err_buf, err_cap, "生成绑定码失败");
        return -1;
    }

    csilk_json_t* data = csilk_json_object();
    csilk_json_add_string(data, "platform", platform);
    csilk_json_add_string(data, "im_user_id", im_user_id);
    csilk_json_add_string(data, "scope", "readonly");
    csilk_json_add_string(data, "bind_token", bind_token);
    csilk_json_add_int(data, "ttl_sec", 365 * 86400);
    csilk_json_add_string(data, "message", "绑定成功，现在可以问我持仓/汇率了");
    free(bind_token);

    *out_data = data;
    *out_code = 0;
    return 0;
}

csilk_json_t*
im_bot_usecase_get_chat_task(void* pool, int64_t user_id, int64_t task_id)
{
    if (!pool || user_id <= 0 || task_id <= 0) {
        return NULL;
    }
    csilk_json_t* task = mf_async_task_get((csilk_db_pool_t*)pool, user_id, task_id);
    if (!task) {
        return NULL;
    }
    const char* task_type = csilk_json_get_string(task, "task_type");
    if (!task_type || strcmp(task_type, "im_chat") != 0) {
        csilk_json_free(task);
        return NULL;
    }
    return task;
}

int
im_bot_usecase_chat_submit(void*          pool,
                           int64_t        user_id,
                           const char*    platform,
                           const char*    im_user_id,
                           const char*    message,
                           const char*    scope_override,
                           csilk_json_t** out_sync_data,
                           int64_t*       out_task_id,
                           bool*          out_deduplicated,
                           int64_t*       out_session_id,
                           int*           out_code,
                           char*          err_buf,
                           size_t         err_cap)
{
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    char             scope[32] = "readonly";
    int64_t bound = mf_im_bot_repo_lookup(db_pool, platform, im_user_id, scope, sizeof(scope));
    if (bound > 0 && scope_override && scope_override[0]) {
        snprintf(scope, sizeof(scope), "%s", scope_override);
    }
    if (bound <= 0 || bound != user_id) {
        csilk_json_t* data = csilk_json_object();
        csilk_json_add_string(data,
                              "reply",
                              "尚未绑定。请在 IM 里发送 /bind <用户名> <密码> 完成绑定，或到 "
                              "App「IM 机器人」生成绑定码。");
        csilk_json_add_int(data, "bound", 0);
        *out_sync_data = data;
        *out_code = 0;
        return 0;
    }

    char sess_title[128];
    snprintf(sess_title, sizeof(sess_title), "IM: %s@%s", platform, im_user_id);
    int64_t session_id = mf_ai_session_find_by_title(db_pool, user_id, sess_title);
    if (session_id <= 0) {
        session_id = mf_ai_session_insert(db_pool, user_id, sess_title, "im-bot", "system");
    }
    if (session_id <= 0) {
        *out_code = 500;
        snprintf(err_buf, err_cap, "创建会话失败");
        return -1;
    }

    mf_ai_message_insert(db_pool, session_id, "user", message, "im-bot", NULL, NULL);

    im_chat_task_arg_t* arg = calloc(1, sizeof(*arg));
    if (!arg) {
        *out_code = 500;
        snprintf(err_buf, err_cap, "内存不足");
        return -1;
    }
    arg->session_id = session_id;
    snprintf(arg->capability, sizeof(arg->capability), "%s", scope);
    arg->message = strdup(message);
    if (!arg->message) {
        free(arg);
        *out_code = 500;
        snprintf(err_buf, err_cap, "内存不足");
        return -1;
    }

    bool    dedup = false;
    int64_t task_id = mf_async_task_submit(db_pool,
                                           user_id,
                                           0,
                                           "im_chat",
                                           NULL,
                                           "正在查询 AI 助手",
                                           im_chat_task_fn,
                                           arg,
                                           im_chat_task_cleanup,
                                           &dedup);
    if (task_id <= 0) {
        *out_code = 500;
        snprintf(err_buf, err_cap, "提交异步 AI 查账任务失败");
        return -1;
    }

    *out_sync_data = NULL;
    *out_task_id = task_id;
    *out_deduplicated = dedup;
    *out_session_id = session_id;
    *out_code = 0;
    return 0;
}
