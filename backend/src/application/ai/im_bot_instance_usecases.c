#include "application/ai/im_bot_instance_usecases.h"

#include "application/ai/im_bot_usecases.h"
#include "application/ai/usecases.h"
#include "application/task/async_task_manager.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include "config/secret.h"
#include "infrastructure/repositories/ai_session_repo_impl.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "services/ai/tools/context.h"
#include "services/ai/tools/dispatcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
im_inst_err(char* err, size_t err_cap, const char* msg)
{
    if (err && err_cap > 0) {
        snprintf(err, err_cap, "%s", msg ? msg : "");
    }
}

static const char*
im_inst_body_str(csilk_json_t* body, const char* key)
{
    if (!body) {
        return NULL;
    }
    const char* v = csilk_json_get_string(body, key);
    return (v && v[0]) ? v : NULL;
}

static csilk_json_t*
im_instance_normalize_row(const csilk_json_t* row)
{
    if (!row || !csilk_json_is_object(row)) {
        return NULL;
    }
    csilk_json_t* out = csilk_json_object();
    if (!out) {
        return NULL;
    }
    size_t n = csilk_json_object_size(row);
    for (size_t i = 0; i < n; i++) {
        const char*         key = csilk_json_object_key(row, i);
        const csilk_json_t* val = csilk_json_object_val(row, i);
        if (!key || !val) {
            continue;
        }
        if (strcmp(key, "id") == 0 || strcmp(key, "mf_user_id") == 0 ||
            strcmp(key, "login_epoch") == 0 || strcmp(key, "verify_attempts") == 0) {
            csilk_json_add_int(out, key, db_get_int(row, key));
        } else if (strcmp(key, "enabled") == 0) {
            csilk_json_add_bool(out, key, db_get_bool(row, key) != 0);
        } else {
            csilk_json_t* copy = csilk_json_copy(val);
            if (copy && !csilk_json_add_object(out, key, copy)) {
                csilk_json_free(copy);
            }
        }
    }
    return out;
}

static csilk_json_t*
im_instance_normalize_owned(csilk_json_t* row)
{
    if (!row) {
        return NULL;
    }
    csilk_json_t* norm = im_instance_normalize_row(row);
    csilk_json_free(row);
    return norm;
}

static csilk_json_t*
im_instance_normalize_list(csilk_json_t* list)
{
    csilk_json_t* out = csilk_json_array();
    if (!out) {
        if (list) {
            csilk_json_free(list);
        }
        return NULL;
    }
    if (list && csilk_json_is_array(list)) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* norm = im_instance_normalize_row(csilk_json_array_get(list, i));
            if (norm && !csilk_json_array_append(out, norm)) {
                csilk_json_free(norm);
            }
        }
    }
    if (list) {
        csilk_json_free(list);
    }
    return out;
}

static int
im_inst_valid_status(const char* status)
{
    static const char* kAllowed[] = {"pending_login",
                                     "qr_ready",
                                     "scanned",
                                     "need_verifycode",
                                     "active",
                                     "session_expired",
                                     "expired",
                                     "error",
                                     NULL};
    if (!status || !status[0]) {
        return 0;
    }
    for (int i = 0; kAllowed[i]; i++) {
        if (strcmp(status, kAllowed[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int
im_instance_usecase_create(void*          pool,
                           int64_t        user_id,
                           const char*    platform,
                           const char*    adapter,
                           csilk_json_t** out,
                           int*           code,
                           char*          err,
                           size_t         err_cap)
{
    if (out) {
        *out = NULL;
    }
    if (code) {
        *code = 0;
    }
    im_inst_err(err, err_cap, "");

    char gateway_token[256] = {0};
    if (!config_secret_get("GATEWAY_TOKEN", gateway_token, sizeof(gateway_token)) ||
        !gateway_token[0]) {
        im_inst_err(err, err_cap, "iLink gateway is not configured");
        if (code) {
            *code = 1004;
        }
        return -1;
    }
    const char* plat = (platform && platform[0]) ? platform : "wechat";
    const char* adp = (adapter && adapter[0]) ? adapter : "ilink";
    if (strcmp(plat, "wechat") != 0 || strcmp(adp, "ilink") != 0) {
        im_inst_err(err, err_cap, "unsupported platform or adapter");
        if (code) {
            *code = 1002;
        }
        return -1;
    }

    int64_t id = mf_im_bot_instance_create((csilk_db_pool_t*)pool, user_id, plat, adp);
    if (id == -2) {
        im_inst_err(err, err_cap, "instance already exists");
        if (code) {
            *code = 1004;
        }
        return -1;
    }
    if (id <= 0) {
        im_inst_err(err, err_cap, "failed to create instance");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    if (out) {
        *out = im_instance_normalize_owned(
            mf_im_bot_instance_get((csilk_db_pool_t*)pool, user_id, id));
    }
    return 0;
}

csilk_json_t*
im_instance_usecase_list_paged(
    void* pool, int64_t user_id, int64_t page, int64_t page_size, int64_t* out_total)
{
    return im_instance_normalize_list(
        mf_im_bot_instance_list_paged((csilk_db_pool_t*)pool, user_id, page, page_size, out_total));
}

csilk_json_t*
im_instance_usecase_list(void* pool, int64_t user_id)
{
    return im_instance_usecase_list_paged(pool, user_id, 1, 500, NULL);
}

csilk_json_t*
im_instance_usecase_get(void* pool, int64_t user_id, int64_t id)
{
    return im_instance_normalize_owned(mf_im_bot_instance_get((csilk_db_pool_t*)pool, user_id, id));
}

int
im_instance_usecase_relogin(
    void* pool, int64_t user_id, int64_t id, int* code, char* err, size_t err_cap)
{
    if (code) {
        *code = 0;
    }
    im_inst_err(err, err_cap, "");
    int rc = mf_im_bot_instance_relogin((csilk_db_pool_t*)pool, user_id, id);
    if (rc == -3) {
        im_inst_err(err, err_cap, "instance not found");
        if (code) {
            *code = 1003;
        }
        return -1;
    }
    if (rc != 0) {
        im_inst_err(err, err_cap, "failed to request relogin");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    return 0;
}

int
im_instance_usecase_verify_code(void*       pool,
                                int64_t     user_id,
                                int64_t     id,
                                const char* code,
                                int*        code_out,
                                char*       err,
                                size_t      err_cap)
{
    if (code_out) {
        *code_out = 0;
    }
    im_inst_err(err, err_cap, "");
    int rc = mf_im_bot_instance_submit_verify_code((csilk_db_pool_t*)pool, user_id, id, code);
    if (rc == -3) {
        im_inst_err(err, err_cap, "instance not found or not waiting for a code");
        if (code_out) {
            *code_out = 1003;
        }
        return -1;
    }
    if (rc == -4) {
        im_inst_err(err, err_cap, "too many verification attempts, please relogin");
        if (code_out) {
            *code_out = 1004;
        }
        return -1;
    }
    if (rc != 0) {
        im_inst_err(err, err_cap, "invalid verification code");
        if (code_out) {
            *code_out = 1002;
        }
        return -1;
    }
    return 0;
}

int
im_instance_usecase_set_enabled(
    void* pool, int64_t user_id, int64_t id, int enabled, int* code, char* err, size_t err_cap)
{
    if (code) {
        *code = 0;
    }
    im_inst_err(err, err_cap, "");
    int rc = mf_im_bot_instance_set_enabled((csilk_db_pool_t*)pool, user_id, id, enabled ? 1 : 0);
    if (rc == -3) {
        im_inst_err(err, err_cap, "instance not found");
        if (code) {
            *code = 1003;
        }
        return -1;
    }
    if (rc != 0) {
        im_inst_err(err, err_cap, "failed to update instance");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    return 0;
}

int
im_instance_usecase_set_capability(void*       pool,
                                   int64_t     user_id,
                                   int64_t     id,
                                   const char* capability,
                                   int*        code,
                                   char*       err,
                                   size_t      err_cap)
{
    if (code) {
        *code = 0;
    }
    im_inst_err(err, err_cap, "");
    if (!capability ||
        (strcmp(capability, "readonly") != 0 && strcmp(capability, "query+confirm") != 0)) {
        im_inst_err(err, err_cap, "invalid capability");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    int rc = mf_im_bot_instance_set_capability((csilk_db_pool_t*)pool, user_id, id, capability);
    if (rc == -3) {
        im_inst_err(err, err_cap, "instance not found");
        if (code) {
            *code = 1003;
        }
        return -1;
    }
    if (rc != 0) {
        im_inst_err(err, err_cap, "failed to update capability");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    return 0;
}

int
im_instance_usecase_delete(
    void* pool, int64_t user_id, int64_t id, int* code, char* err, size_t err_cap)
{
    if (code) {
        *code = 0;
    }
    im_inst_err(err, err_cap, "");
    int rc = mf_im_bot_instance_delete((csilk_db_pool_t*)pool, user_id, id);
    if (rc == -3) {
        im_inst_err(err, err_cap, "instance not found");
        if (code) {
            *code = 1003;
        }
        return -1;
    }
    if (rc != 0) {
        im_inst_err(err, err_cap, "failed to delete instance");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    return 0;
}

csilk_json_t*
im_instance_gateway_list(void* pool)
{
    return im_instance_normalize_list(mf_im_bot_instance_list_all((csilk_db_pool_t*)pool));
}

csilk_json_t*
im_instance_gateway_get(void* pool, int64_t id)
{
    csilk_json_t* inst =
        im_instance_normalize_owned(mf_im_bot_instance_get_any((csilk_db_pool_t*)pool, id));
    if (!inst) {
        return NULL;
    }
    char pin[64] = {0};
    if (mf_im_bot_instance_consume_verify_code((csilk_db_pool_t*)pool, id, pin, sizeof(pin)) == 1 &&
        pin[0]) {
        csilk_json_add_string(inst, "verify_code", pin);
    }
    return inst;
}

int
im_instance_gateway_status(
    void* pool, int64_t id, csilk_json_t* body, int* code, char* err, size_t err_cap)
{
    if (code) {
        *code = 0;
    }
    im_inst_err(err, err_cap, "");
    const char* status = im_inst_body_str(body, "status");
    if (!im_inst_valid_status(status)) {
        im_inst_err(err, err_cap, "invalid or forbidden status");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    if (strcmp(status, "active") == 0) {
        const char* bot_id = im_inst_body_str(body, "ilink_bot_id");
        const char* user_id = im_inst_body_str(body, "ilink_user_id");
        if (!bot_id || !user_id) {
            im_inst_err(err, err_cap, "ilink_bot_id and ilink_user_id are required");
            if (code) {
                *code = 1002;
            }
            return -1;
        }
        char conflict[256] = {0};
        int  rc = mf_im_bot_instance_activate(
            (csilk_db_pool_t*)pool, id, bot_id, user_id, conflict, sizeof(conflict));
        if (rc == -2 || rc == -3) {
            im_inst_err(err, err_cap, conflict[0] ? conflict : "instance or binding conflict");
            if (code) {
                *code = 1004;
            }
            return -1;
        }
        if (rc != 0) {
            im_inst_err(err, err_cap, "failed to activate instance");
            if (code) {
                *code = 1002;
            }
            return -1;
        }
        return 0;
    }
    const char* qr = im_inst_body_str(body, "qr_content");
    if (strcmp(status, "qr_ready") == 0 && !qr) {
        im_inst_err(err, err_cap, "qr_content is required for qr_ready");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    int rc = mf_im_bot_instance_update_status((csilk_db_pool_t*)pool,
                                              id,
                                              status,
                                              qr,
                                              im_inst_body_str(body, "qr_expires_at"),
                                              im_inst_body_str(body, "last_error"));
    if (rc == -3) {
        im_inst_err(err, err_cap, "instance not found");
        if (code) {
            *code = 1003;
        }
        return -1;
    }
    if (rc != 0) {
        im_inst_err(err, err_cap, "failed to update status");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    return 0;
}

int
im_instance_gateway_heartbeat_ext(void*          pool,
                                  int64_t        id,
                                  const char*    last_inbound_at,
                                  csilk_json_t** out,
                                  int*           code,
                                  char*          err,
                                  size_t         err_cap)
{
    if (out) {
        *out = NULL;
    }
    if (code) {
        *code = 0;
    }
    im_inst_err(err, err_cap, "");
    char    status[64] = {0};
    int64_t epoch = 0;
    int     enabled = 0;
    int     rc = mf_im_bot_instance_heartbeat_with_inbound(
        (csilk_db_pool_t*)pool, id, last_inbound_at, status, sizeof(status), &epoch, &enabled);
    if (rc == -3) {
        im_inst_err(err, err_cap, "instance not found");
        if (code) {
            *code = 1003;
        }
        return -1;
    }
    if (rc != 0) {
        im_inst_err(err, err_cap, "heartbeat failed");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    if (!enabled) {
        im_inst_err(err, err_cap, "instance is disabled");
        if (code) {
            *code = 1004;
        }
        return -1;
    }
    if (out) {
        csilk_json_t* data = csilk_json_object();
        csilk_json_add_string(data, "status", status);
        csilk_json_add_int(data, "login_epoch", (int64_t)epoch);
        csilk_json_add_bool(data, "enabled", enabled != 0);
        *out = data;
    }
    return 0;
}

int
im_instance_gateway_heartbeat(
    void* pool, int64_t id, csilk_json_t** out, int* code, char* err, size_t err_cap)
{
    return im_instance_gateway_heartbeat_ext(pool, id, NULL, out, code, err, err_cap);
}

int
im_instance_gateway_chat(void*          pool,
                         int64_t        id,
                         const char*    im_user_id,
                         const char*    message,
                         csilk_json_t** out_sync,
                         int64_t*       task_id,
                         bool*          dedup,
                         int64_t*       session_id,
                         int*           code,
                         char*          err,
                         size_t         err_cap)
{
    if (out_sync) {
        *out_sync = NULL;
    }
    if (code) {
        *code = 0;
    }
    im_inst_err(err, err_cap, "");
    if (!message || !message[0] || !im_user_id || !im_user_id[0]) {
        im_inst_err(err, err_cap, "message and im_user_id are required");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    csilk_json_t* inst = mf_im_bot_instance_get_any((csilk_db_pool_t*)pool, id);
    if (!inst) {
        im_inst_err(err, err_cap, "instance not found");
        if (code) {
            *code = 1003;
        }
        return -1;
    }
    const char* bound = csilk_json_get_string(inst, "im_user_id");
    int         enabled = db_get_bool(inst, "enabled") ? 1 : 0;
    const char* status = csilk_json_get_string(inst, "status");
    if (!enabled || !status || strcmp(status, "active") != 0 || !bound ||
        strcmp(bound, im_user_id) != 0) {
        csilk_json_free(inst);
        im_inst_err(err, err_cap, "instance is unavailable for this sender");
        if (code) {
            *code = 1004;
        }
        return -1;
    }
    int64_t     owner = db_get_int(inst, "mf_user_id");
    char        capability[32] = "readonly";
    const char* cap_ptr = csilk_json_get_string(inst, "capability");
    if (cap_ptr && cap_ptr[0]) {
        snprintf(capability, sizeof(capability), "%s", cap_ptr);
    }
    csilk_json_free(inst);
    if (owner <= 0) {
        im_inst_err(err, err_cap, "invalid instance owner");
        if (code) {
            *code = 1004;
        }
        return -1;
    }
    int rc = im_bot_usecase_chat_submit(pool,
                                        owner,
                                        "wechat",
                                        im_user_id,
                                        message,
                                        capability,
                                        out_sync,
                                        task_id,
                                        dedup,
                                        session_id,
                                        code,
                                        err,
                                        err_cap);
    return rc;
}

int
im_instance_gateway_confirm(void*       pool,
                            int64_t     id,
                            const char* im_user_id,
                            const char* confirm_code,
                            char**      out_reply,
                            int*        code,
                            char*       err,
                            size_t      err_cap)
{
    if (out_reply) {
        *out_reply = NULL;
    }
    if (code) {
        *code = 0;
    }
    im_inst_err(err, err_cap, "");
    if (!im_user_id || !im_user_id[0] || !confirm_code || !confirm_code[0]) {
        im_inst_err(err, err_cap, "im_user_id 与确认码不能为空");
        if (code) {
            *code = 1002;
        }
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    inst = mf_im_bot_instance_get_any(db_pool, id);
    if (!inst) {
        im_inst_err(err, err_cap, "instance not found");
        if (code) {
            *code = 1003;
        }
        return -1;
    }
    const char* bound_ptr = csilk_json_get_string(inst, "im_user_id");
    const char* status_ptr = csilk_json_get_string(inst, "status");
    const char* plat_ptr = csilk_json_get_string(inst, "platform");
    const char* cap_ptr = csilk_json_get_string(inst, "capability");
    int         enabled = db_get_bool(inst, "enabled") ? 1 : 0;
    int64_t     owner = db_get_int(inst, "mf_user_id");
    char        bound[256] = "";
    char        status[32] = "";
    char        plat[32] = "wechat";
    char        cap[32] = "readonly";
    if (bound_ptr && bound_ptr[0]) {
        snprintf(bound, sizeof(bound), "%s", bound_ptr);
    }
    if (status_ptr && status_ptr[0]) {
        snprintf(status, sizeof(status), "%s", status_ptr);
    }
    if (plat_ptr && plat_ptr[0]) {
        snprintf(plat, sizeof(plat), "%s", plat_ptr);
    }
    if (cap_ptr && cap_ptr[0]) {
        snprintf(cap, sizeof(cap), "%s", cap_ptr);
    }
    csilk_json_free(inst);

    if (!enabled || !status[0] || strcmp(status, "active") != 0 || !bound[0] ||
        strcmp(bound, im_user_id) != 0 || owner <= 0) {
        im_inst_err(err, err_cap, "instance is unavailable for this sender");
        if (code) {
            *code = 1004;
        }
        return -1;
    }
    if (strcmp(cap, "query+confirm") != 0) {
        im_inst_err(err, err_cap, "该实例未开启微信记账能力");
        if (code) {
            *code = 1004;
        }
        return -1;
    }

    char title[192];
    snprintf(title, sizeof(title), "IM: %s@%s", plat, im_user_id);
    int64_t session_id = mf_ai_session_find_by_title(db_pool, owner, title);
    if (session_id <= 0) {
        im_inst_err(err, err_cap, "没有待确认的记账草案");
        if (code) {
            *code = 1004;
        }
        return -1;
    }
    csilk_json_t* draft =
        mf_im_bot_pending_action_get(db_pool, confirm_code, owner, session_id, (int64_t)time(NULL));
    if (!draft) {
        im_inst_err(err, err_cap, "确认码无效、已过期或已使用");
        if (code) {
            *code = 1004;
        }
        return -1;
    }
    const char* action_type = csilk_json_get_string(draft, "action_type");
    const char* token_ptr = csilk_json_get_string(draft, "draft_token");
    const char* args_ptr = csilk_json_get_string(draft, "args_json");
    char        token_buf[600] = {0};
    char*       args_buf = NULL;
    if (token_ptr && token_ptr[0]) {
        snprintf(token_buf, sizeof(token_buf), "%s", token_ptr);
    }
    if (args_ptr && args_ptr[0]) {
        args_buf = strdup(args_ptr);
    }
    if (!action_type || strcmp(action_type, "daily_expense") != 0 || !token_buf[0] || !args_buf) {
        csilk_json_free(draft);
        free(args_buf);
        im_inst_err(err, err_cap, "不支持的确认草案类型");
        if (code) {
            *code = 1004;
        }
        return -1;
    }

    int64_t ledger_id = ledger_get_default(db_pool, owner);
    char    role[32] = {0};
    if (ledger_id <= 0 || !ledger_get_user_role(db_pool, ledger_id, owner, role, sizeof(role)) ||
        (strcmp(role, "owner") != 0 && strcmp(role, "editor") != 0)) {
        csilk_json_free(draft);
        free(args_buf);
        im_inst_err(err, err_cap, "默认账本无编辑权限");
        if (code) {
            *code = 1004;
        }
        return -1;
    }

    csilk_json_t* parsed_args = csilk_json_parse(args_buf);
    csilk_json_free(draft);
    free(args_buf);
    if (!parsed_args || !csilk_json_is_object(parsed_args)) {
        if (parsed_args) {
            csilk_json_free(parsed_args);
        }
        im_inst_err(err, err_cap, "草案参数损坏");
        if (code) {
            *code = 1004;
        }
        return -1;
    }
    /* 解析出的 JSON 为只读视图，需复制到可变对象后才能追加 draft_token。 */
    csilk_json_t* args = csilk_json_object();
    size_t        nargs = csilk_json_object_size(parsed_args);
    for (size_t i = 0; i < nargs; i++) {
        const char*         key = csilk_json_object_key(parsed_args, i);
        const csilk_json_t* val = csilk_json_object_val(parsed_args, i);
        if (key && val) {
            csilk_json_add_object(args, key, csilk_json_copy((csilk_json_t*)val));
        }
    }
    csilk_json_free(parsed_args);
    csilk_json_add_string(args, "draft_token", token_buf);

    ai_tool_context_t* ctx = ai_tool_context_create(db_pool, owner, session_id, NULL);
    if (!ctx) {
        csilk_json_free(args);
        im_inst_err(err, err_cap, "确认执行失败");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    csilk_json_add_int(ctx->metadata, "ledger_id", ledger_id);
    const char* type_ptr = csilk_json_get_string(args, "type");
    double      amount_val = csilk_json_get_number(args, "amount");
    char        type_buf[16] = "expense";
    char        amount_buf[64] = "0.00";
    if (type_ptr && type_ptr[0]) {
        snprintf(type_buf, sizeof(type_buf), "%s", type_ptr);
    }
    snprintf(amount_buf, sizeof(amount_buf), "%.2f", amount_val);

    char* result = ai_tool_dispatch_parsed(ctx, "confirm_proposed_expense", args);
    ai_tool_context_free(ctx);
    csilk_json_free(args);

    if (!result) {
        im_inst_err(err, err_cap, "确认执行失败");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    csilk_json_t* parsed = csilk_json_parse(result);
    if (!parsed || !csilk_json_get_bool(parsed, "success")) {
        const char* em = parsed ? csilk_json_get_string(parsed, "error") : NULL;
        im_inst_err(err, err_cap, em && em[0] ? em : "草案已过期或已被使用");
        if (code) {
            *code = 1004;
        }
        if (parsed) {
            csilk_json_free(parsed);
        }
        free(result);
        return -1;
    }
    csilk_json_free(parsed);
    free(result);

    mf_im_bot_pending_action_consume(db_pool, confirm_code, owner, session_id);
    ai_usecase_mark_action_executed(db_pool, owner, token_buf);

    if (out_reply) {
        size_t need = strlen(amount_buf) + 128;
        char*  reply = (char*)malloc(need);
        if (reply) {
            snprintf(reply,
                     need,
                     "已记账：%s ¥%s，已写入默认账本并同步账户余额。",
                     strcmp(type_buf, "income") == 0 ? "收入" : "支出",
                     amount_buf);
            *out_reply = reply;
        }
    }
    return 0;
}

csilk_json_t*
im_instance_gateway_task(void* pool, int64_t id, int64_t task_id, int* code)
{
    if (code) {
        *code = 0;
    }
    csilk_json_t* inst = mf_im_bot_instance_get_any((csilk_db_pool_t*)pool, id);
    if (!inst) {
        if (code) {
            *code = 1003;
        }
        return NULL;
    }
    int64_t owner = db_get_int(inst, "mf_user_id");
    int     enabled = db_get_bool(inst, "enabled") ? 1 : 0;
    csilk_json_free(inst);
    if (owner <= 0 || !enabled) {
        if (code) {
            *code = 1003;
        }
        return NULL;
    }
    csilk_json_t* task = mf_async_task_get((csilk_db_pool_t*)pool, owner, task_id);
    if (!task) {
        if (code) {
            *code = 1003;
        }
        return NULL;
    }
    const char* type = csilk_json_get_string(task, "task_type");
    if (!type || strcmp(type, "im_chat") != 0) {
        csilk_json_free(task);
        if (code) {
            *code = 1003;
        }
        return NULL;
    }
    return task;
}
