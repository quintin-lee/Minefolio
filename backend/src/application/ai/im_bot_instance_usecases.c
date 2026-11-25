#include "application/ai/im_bot_instance_usecases.h"

#include "application/ai/im_bot_usecases.h"
#include "application/task/async_task_manager.h"
#include "common/db.h"
#include "config/secret.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"

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
        *out = mf_im_bot_instance_get((csilk_db_pool_t*)pool, user_id, id);
    }
    return 0;
}

csilk_json_t*
im_instance_usecase_list(void* pool, int64_t user_id)
{
    return mf_im_bot_instance_list((csilk_db_pool_t*)pool, user_id);
}

csilk_json_t*
im_instance_usecase_get(void* pool, int64_t user_id, int64_t id)
{
    return mf_im_bot_instance_get((csilk_db_pool_t*)pool, user_id, id);
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
    return mf_im_bot_instance_list_all((csilk_db_pool_t*)pool);
}

csilk_json_t*
im_instance_gateway_get(void* pool, int64_t id)
{
    csilk_json_t* inst = mf_im_bot_instance_get_any((csilk_db_pool_t*)pool, id);
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
        csilk_json_add_int(data, "enabled", enabled ? 1 : 0);
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
    int64_t owner = db_get_int(inst, "mf_user_id");
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
                                        out_sync,
                                        task_id,
                                        dedup,
                                        session_id,
                                        code,
                                        err,
                                        err_cap);
    return rc;
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
