#include "interfaces/http/controllers/scheduler_controller.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "application/scheduler/dtos.h"
#include "application/scheduler/usecases.h"
#include "application/auth/usecases.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/response.h"
#include "core/scheduler/scheduler_engine.h"

static bool
is_current_user_admin(int64_t user_id)
{
    return auth_usecase_is_instance_admin(db_get_pool(), user_id) != 0;
}

void
api_scheduler_dashboard(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    bool          is_admin = is_current_user_admin(user_id);
    csilk_json_t* resp = NULL;
    int           rc = scheduler_usecase_dashboard(db_get_pool(), user_id, is_admin, &resp);
    if (rc != 0 || !resp) {
        respond_error(c, 500, "Failed to load scheduler dashboard");
        return;
    }
    respond_ok(c, resp);
}

void
api_scheduler_jobs_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    bool is_admin = is_current_user_admin(user_id);

    int64_t page = 1;
    int64_t page_size = 20;

    const char* p_str = csilk_get_query(c, "page");
    if (p_str && *p_str) {
        page = parse_query_int_strict(p_str, 1);
    }
    const char* ps_str = csilk_get_query(c, "page_size");
    if (ps_str && *ps_str) {
        page_size = parse_query_int_strict(ps_str, 20);
    }

    csilk_json_t* resp = NULL;
    int rc = scheduler_usecase_job_list(db_get_pool(), user_id, is_admin, page, page_size, &resp);
    if (rc != 0 || !resp) {
        respond_error(c, 500, "Failed to list scheduled jobs");
        return;
    }
    respond_ok(c, resp);
}

void
api_scheduler_jobs_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON payload");
        return;
    }

    const char* name = csilk_json_get_string(body, "name");
    const char* desc = csilk_json_get_string(body, "description");
    const char* job_type = csilk_json_get_string(body, "job_type");
    const char* trigger_type = csilk_json_get_string(body, "trigger_type");

    if (!name || !*name || !job_type || !*job_type || !trigger_type || !*trigger_type) {
        csilk_json_free(body);
        respond_bad_request(c, "Missing required fields: name, job_type, trigger_type");
        return;
    }

    job_create_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.name, sizeof(dto.name), "%s", name);
    if (desc) {
        snprintf(dto.description, sizeof(dto.description), "%s", desc);
    }
    snprintf(dto.job_type, sizeof(dto.job_type), "%s", job_type);
    snprintf(dto.trigger_type, sizeof(dto.trigger_type), "%s", trigger_type);

    /* Serialized or string configs */
    csilk_json_t* tcfg = csilk_json_get(body, "trigger_config");
    if (tcfg) {
        if (csilk_json_is_string(tcfg)) {
            snprintf(dto.trigger_config,
                     sizeof(dto.trigger_config),
                     "%s",
                     csilk_json_string_value(tcfg));
        } else {
            size_t slen = 0;
            char*  s = csilk_json_serialize(tcfg, &slen);
            if (s) {
                snprintf(dto.trigger_config, sizeof(dto.trigger_config), "%s", s);
                free(s);
            }
        }
    }

    csilk_json_t* pcfg = csilk_json_get(body, "payload_config");
    if (pcfg) {
        if (csilk_json_is_string(pcfg)) {
            snprintf(dto.payload_config,
                     sizeof(dto.payload_config),
                     "%s",
                     csilk_json_string_value(pcfg));
        } else {
            size_t slen = 0;
            char*  s = csilk_json_serialize(pcfg, &slen);
            if (s) {
                snprintf(dto.payload_config, sizeof(dto.payload_config), "%s", s);
                free(s);
            }
        }
    }

    const char* dc = csilk_json_get_string(body, "delivery_channel");
    if (dc) {
        snprintf(dto.delivery_channel, sizeof(dto.delivery_channel), "%s", dc);
    } else {
        snprintf(dto.delivery_channel, sizeof(dto.delivery_channel), "user_default");
    }

    csilk_json_t* dcfg = csilk_json_get(body, "delivery_config");
    if (dcfg) {
        if (csilk_json_is_string(dcfg)) {
            snprintf(dto.delivery_config,
                     sizeof(dto.delivery_config),
                     "%s",
                     csilk_json_string_value(dcfg));
        } else {
            size_t slen = 0;
            char*  s = csilk_json_serialize(dcfg, &slen);
            if (s) {
                snprintf(dto.delivery_config, sizeof(dto.delivery_config), "%s", s);
                free(s);
            }
        }
    }

    csilk_json_free(body);

    csilk_json_t* new_job = NULL;
    int           rc = scheduler_usecase_job_create(db_get_pool(), user_id, &dto, &new_job);
    if (rc != 0 || !new_job) {
        respond_error(c, 500, "Failed to create scheduled job");
        return;
    }
    respond_ok(c, new_job);
}

void
api_scheduler_jobs_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str || !*id_str) {
        respond_bad_request(c, "Missing job id");
        return;
    }
    int64_t job_id = parse_query_int_strict(id_str, -1);
    if (job_id <= 0) {
        respond_bad_request(c, "Invalid job id");
        return;
    }

    bool          is_admin = is_current_user_admin(user_id);
    csilk_json_t* job = NULL;
    int           rc = scheduler_usecase_job_get(db_get_pool(), user_id, is_admin, job_id, &job);
    if (rc != 0 || !job) {
        respond_not_found(c);
        return;
    }
    respond_ok(c, job);
}

void
api_scheduler_jobs_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str || !*id_str) {
        respond_bad_request(c, "Missing job id");
        return;
    }
    int64_t job_id = parse_query_int_strict(id_str, -1);
    if (job_id <= 0) {
        respond_bad_request(c, "Invalid job id");
        return;
    }

    bool          is_admin = is_current_user_admin(user_id);
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON payload");
        return;
    }

    const char* name = csilk_json_get_string(body, "name");
    const char* desc = csilk_json_get_string(body, "description");
    const char* trigger_type = csilk_json_get_string(body, "trigger_type");

    job_update_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    if (name) {
        snprintf(dto.name, sizeof(dto.name), "%s", name);
    }
    if (desc) {
        snprintf(dto.description, sizeof(dto.description), "%s", desc);
    }
    if (trigger_type) {
        snprintf(dto.trigger_type, sizeof(dto.trigger_type), "%s", trigger_type);
    }

    csilk_json_t* tcfg = csilk_json_get(body, "trigger_config");
    if (tcfg) {
        if (csilk_json_is_string(tcfg)) {
            snprintf(dto.trigger_config,
                     sizeof(dto.trigger_config),
                     "%s",
                     csilk_json_string_value(tcfg));
        } else {
            size_t slen = 0;
            char*  s = csilk_json_serialize(tcfg, &slen);
            if (s) {
                snprintf(dto.trigger_config, sizeof(dto.trigger_config), "%s", s);
                free(s);
            }
        }
    }

    csilk_json_t* pcfg = csilk_json_get(body, "payload_config");
    if (pcfg) {
        if (csilk_json_is_string(pcfg)) {
            snprintf(dto.payload_config,
                     sizeof(dto.payload_config),
                     "%s",
                     csilk_json_string_value(pcfg));
        } else {
            size_t slen = 0;
            char*  s = csilk_json_serialize(pcfg, &slen);
            if (s) {
                snprintf(dto.payload_config, sizeof(dto.payload_config), "%s", s);
                free(s);
            }
        }
    }

    const char* dc = csilk_json_get_string(body, "delivery_channel");
    if (dc) {
        snprintf(dto.delivery_channel, sizeof(dto.delivery_channel), "%s", dc);
    }

    csilk_json_t* dcfg = csilk_json_get(body, "delivery_config");
    if (dcfg) {
        if (csilk_json_is_string(dcfg)) {
            snprintf(dto.delivery_config,
                     sizeof(dto.delivery_config),
                     "%s",
                     csilk_json_string_value(dcfg));
        } else {
            size_t slen = 0;
            char*  s = csilk_json_serialize(dcfg, &slen);
            if (s) {
                snprintf(dto.delivery_config, sizeof(dto.delivery_config), "%s", s);
                free(s);
            }
        }
    }

    csilk_json_free(body);

    int rc = scheduler_usecase_job_update(db_get_pool(), user_id, is_admin, job_id, &dto);
    if (rc == 1004) {
        respond_error(c, 1004, "Forbidden: not the owner or permission denied");
        return;
    }
    if (rc != 0) {
        respond_error(c, 500, "Failed to update scheduled job");
        return;
    }

    csilk_json_t* ret = csilk_json_object();
    csilk_json_add_number(ret, "id", (double)job_id);
    respond_ok(c, ret);
}

void
api_scheduler_jobs_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str || !*id_str) {
        respond_bad_request(c, "Missing job id");
        return;
    }
    int64_t job_id = parse_query_int_strict(id_str, -1);
    if (job_id <= 0) {
        respond_bad_request(c, "Invalid job id");
        return;
    }

    int rc = scheduler_usecase_job_delete(db_get_pool(), user_id, job_id);
    if (rc == 1004) {
        respond_error(c, 1004, "系统内置任务不可删除，如不需要可将其停用");
        return;
    }
    if (rc != 0) {
        respond_error(c, 500, "Failed to delete scheduled job");
        return;
    }

    csilk_json_t* ret = csilk_json_object();
    csilk_json_add_number(ret, "id", (double)job_id);
    respond_ok(c, ret);
}

void
api_scheduler_jobs_toggle(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str || !*id_str) {
        respond_bad_request(c, "Missing job id");
        return;
    }
    int64_t job_id = parse_query_int_strict(id_str, -1);
    if (job_id <= 0) {
        respond_bad_request(c, "Invalid job id");
        return;
    }

    bool          is_admin = is_current_user_admin(user_id);
    csilk_json_t* body = csilk_bind_json(c);
    bool          enabled = true;
    if (body) {
        if (csilk_json_get(body, "enabled")) {
            enabled = csilk_json_get_bool(body, "enabled");
        }
        csilk_json_free(body);
    }

    int rc = scheduler_usecase_job_toggle(db_get_pool(), user_id, is_admin, job_id, enabled);
    if (rc == 1004) {
        respond_error(c, 1004, "Forbidden: permission denied");
        return;
    }
    if (rc != 0) {
        respond_error(c, 500, "Failed to toggle job");
        return;
    }

    csilk_json_t* ret = csilk_json_object();
    csilk_json_add_number(ret, "id", (double)job_id);
    csilk_json_add_bool(ret, "enabled", enabled);
    respond_ok(c, ret);
}

void
api_scheduler_jobs_run(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str || !*id_str) {
        respond_bad_request(c, "Missing job id");
        return;
    }
    int64_t job_id = parse_query_int_strict(id_str, -1);
    if (job_id <= 0) {
        respond_bad_request(c, "Invalid job id");
        return;
    }

    bool is_admin = is_current_user_admin(user_id);
    int  rc = scheduler_usecase_job_run_now(db_get_pool(), user_id, is_admin, job_id);
    if (rc == 1004) {
        respond_error(c, 1004, "Forbidden: permission denied");
        return;
    }
    if (rc != 0) {
        respond_error(c, 500, "Failed to schedule job run");
        return;
    }

    csilk_json_t* ret = csilk_json_object();
    csilk_json_add_string(ret, "message", "Job execution scheduled successfully");
    respond_ok(c, ret);
}

void
api_scheduler_jobs_logs(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     job_id = id_str ? parse_query_int_strict(id_str, 0) : 0;

    bool is_admin = is_current_user_admin(user_id);

    int64_t     page = 1;
    int64_t     page_size = 20;
    const char* p_str = csilk_get_query(c, "page");
    if (p_str && *p_str) {
        page = parse_query_int_strict(p_str, 1);
    }
    const char* ps_str = csilk_get_query(c, "page_size");
    if (ps_str && *ps_str) {
        page_size = parse_query_int_strict(ps_str, 20);
    }

    csilk_json_t* resp = NULL;
    int           rc = scheduler_usecase_job_logs(
        db_get_pool(), user_id, is_admin, job_id, page, page_size, &resp);
    if (rc == 1004) {
        respond_error(c, 1004, "Forbidden: permission denied");
        return;
    }
    if (rc != 0 || !resp) {
        respond_error(c, 500, "Failed to fetch job logs");
        return;
    }
    respond_ok(c, resp);
}

void
api_scheduler_channel_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* resp = NULL;
    int           rc = scheduler_usecase_get_channel(db_get_pool(), user_id, &resp);
    if (rc != 0 || !resp) {
        respond_error(c, 500, "Failed to get user notification channel");
        return;
    }
    respond_ok(c, resp);
}

void
api_scheduler_channel_save(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON payload");
        return;
    }

    const char* ct = csilk_json_get_string(body, "channel_type");
    if (!ct || !*ct) {
        csilk_json_free(body);
        respond_bad_request(c, "Missing channel_type");
        return;
    }

    channel_save_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "%s", ct);

    const char* cn = csilk_json_get_string(body, "channel_name");
    if (cn) {
        snprintf(dto.channel_name, sizeof(dto.channel_name), "%s", cn);
    } else {
        snprintf(dto.channel_name, sizeof(dto.channel_name), "个人微信");
    }

    const char* wu = csilk_json_get_string(body, "webhook_url");
    if (wu) {
        snprintf(dto.webhook_url, sizeof(dto.webhook_url), "%s", wu);
    }

    const char* sk = csilk_json_get_string(body, "secret_key");
    if (sk) {
        snprintf(dto.secret_key, sizeof(dto.secret_key), "%s", sk);
    }

    csilk_json_free(body);

    int rc = scheduler_usecase_save_channel(db_get_pool(), user_id, &dto);
    if (rc == 1002) {
        respond_error(c, 1002, "不支持的推送服务类型");
        return;
    }
    if (rc != 0) {
        respond_error(c, 500, "Failed to save user notification channel");
        return;
    }

    csilk_json_t* ret = csilk_json_object();
    csilk_json_add_string(ret, "message", "Notification channel saved");
    respond_ok(c, ret);
}

#define CHANNEL_TEST_MAX_SLOTS 16
#define CHANNEL_TEST_STALE_SEC 30

typedef enum {
    SLOT_FREE = 0,
    SLOT_RUNNING = 1,
    SLOT_DISPATCHED = 2,
} channel_test_slot_state_t;

typedef struct {
    channel_test_slot_state_t state;
    uint64_t                  generation;
    csilk_async_op_t*         op;
    csilk_ctx_t*              ctx;
    uint64_t                  request_seq;
    int64_t                   user_id;
    int                       rc;
    char                      err_buf[256];
    time_t                    dispatched_at;
} channel_test_slot_t;

static channel_test_slot_t s_test_slots[CHANNEL_TEST_MAX_SLOTS];
static pthread_mutex_t     s_test_slots_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t            s_test_slot_gen = 0;

static time_t
channel_test_mono_sec(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return ts.tv_sec;
    }
    return time(NULL);
}

static int
channel_test_is_alive(void* arg)
{
    return csilk_async_op_is_alive((const csilk_async_op_t*)arg);
}

static void
send_channel_test_async_json(csilk_ctx_t* c, int rc, const char* err_msg)
{
    if (!c || csilk_ctx_is_closed(c)) {
        return;
    }

    csilk_json_t* root = csilk_json_object();
    if (root) {
        if (rc == 0) {
            csilk_json_t* data = csilk_json_object();
            if (data) {
                csilk_json_add_string(data, "message", "测试消息已成功发送至您的微信！");
                csilk_json_add_int(root, "code", 0);
                csilk_json_add_string(root, "message", "ok");
                csilk_json_add_object(root, "data", data);
            } else {
                csilk_json_free(root);
                root = NULL;
            }
        } else {
            csilk_json_add_int(root, "code", 400);
            csilk_json_add_string(
                root, "message", (err_msg && err_msg[0]) ? err_msg : "测试消息发送失败");
        }
    }

    size_t body_len = 0;
    char*  body = root ? csilk_json_serialize(root, &body_len) : NULL;
    if (root) {
        csilk_json_free(root);
    }

    static const char kFallbackErr[] = "{\"code\":500,\"message\":\"Internal Server Error\"}";
    const char*       out_buf = body ? body : kFallbackErr;
    size_t            out_len = body ? body_len : (sizeof(kFallbackErr) - 1);

    csilk_status(c, CSILK_STATUS_OK);
    csilk_set_header(c, "Content-Type", "application/json");
    int wrc = csilk_response_write(c, (const uint8_t*)out_buf, out_len);
    free(body);
    if (wrc >= 0) {
        csilk_response_end(c);
    }
}

static void
on_channel_test_complete(csilk_ctx_t* c, void* result)
{
    channel_test_slot_t* slot = (channel_test_slot_t*)result;
    int                  rc = -1;
    char                 err_buf[256];
    err_buf[0] = '\0';
    bool valid = false;

    if (slot && c) {
        uint64_t c_seq = csilk_ctx_get_request_seq(c);
        pthread_mutex_lock(&s_test_slots_lock);
        if (slot->state == SLOT_DISPATCHED && slot->ctx == c && slot->request_seq == c_seq) {
            rc = slot->rc;
            snprintf(err_buf, sizeof(err_buf), "%s", slot->err_buf);
            slot->state = SLOT_FREE;
            slot->op = NULL;
            slot->ctx = NULL;
            slot->request_seq = 0;
            slot->user_id = 0;
            slot->rc = 0;
            slot->err_buf[0] = '\0';
            slot->dispatched_at = 0;
            valid = true;
        }
        pthread_mutex_unlock(&s_test_slots_lock);
    }

    if (!valid) {
        snprintf(err_buf, sizeof(err_buf), "%s", "测试请求状态已过期");
    }
    send_channel_test_async_json(c, rc, err_buf);
}

static void
channel_test_worker(void* arg)
{
    channel_test_slot_t* slot = (channel_test_slot_t*)arg;
    if (!slot) {
        return;
    }

    pthread_mutex_lock(&s_test_slots_lock);
    csilk_async_op_t* op = slot->op;
    int64_t           user_id = slot->user_id;
    uint64_t          gen = slot->generation;
    pthread_mutex_unlock(&s_test_slots_lock);

    if (!op || !csilk_async_op_is_alive(op)) {
        if (op) {
            csilk_async_op_cancel(op);
        }
        pthread_mutex_lock(&s_test_slots_lock);
        if (slot->state == SLOT_RUNNING && slot->generation == gen) {
            slot->state = SLOT_FREE;
            slot->op = NULL;
            slot->ctx = NULL;
            slot->request_seq = 0;
            slot->user_id = 0;
            slot->err_buf[0] = '\0';
            slot->dispatched_at = 0;
        }
        pthread_mutex_unlock(&s_test_slots_lock);
        return;
    }

    char err_buf[256];
    err_buf[0] = '\0';
    int rc = scheduler_usecase_test_channel_cancellable(
        db_get_pool(), user_id, channel_test_is_alive, op, err_buf, sizeof(err_buf));

    pthread_mutex_lock(&s_test_slots_lock);
    if (slot->state != SLOT_RUNNING || slot->generation != gen) {
        pthread_mutex_unlock(&s_test_slots_lock);
        csilk_async_op_cancel(op);
        return;
    }

    if (!csilk_async_op_is_alive(op)) {
        slot->state = SLOT_FREE;
        slot->op = NULL;
        slot->ctx = NULL;
        slot->request_seq = 0;
        slot->user_id = 0;
        slot->rc = 0;
        slot->err_buf[0] = '\0';
        slot->dispatched_at = 0;
        pthread_mutex_unlock(&s_test_slots_lock);
        csilk_async_op_cancel(op);
        return;
    }

    slot->rc = rc;
    snprintf(slot->err_buf, sizeof(slot->err_buf), "%s", err_buf);
    slot->state = SLOT_DISPATCHED;
    slot->dispatched_at = channel_test_mono_sec();
    pthread_mutex_unlock(&s_test_slots_lock);

    if (csilk_async_op_complete(op, slot) < 0) {
        pthread_mutex_lock(&s_test_slots_lock);
        if (slot->state == SLOT_DISPATCHED && slot->generation == gen) {
            slot->state = SLOT_FREE;
            slot->op = NULL;
            slot->ctx = NULL;
            slot->request_seq = 0;
            slot->user_id = 0;
            slot->rc = 0;
            slot->err_buf[0] = '\0';
            slot->dispatched_at = 0;
        }
        pthread_mutex_unlock(&s_test_slots_lock);
    }
}

void
api_scheduler_channel_test(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    if (!scheduler_engine_is_running()) {
        char err_buf[256];
        err_buf[0] = '\0';
        int rc = scheduler_usecase_test_channel(db_get_pool(), user_id, err_buf, sizeof(err_buf));
        if (rc != 0) {
            respond_error(c, 400, err_buf[0] ? err_buf : "测试消息发送失败");
            return;
        }

        csilk_json_t* ret = csilk_json_object();
        csilk_json_add_string(ret, "message", "测试消息已成功发送至您的微信！");
        respond_ok(c, ret);
        return;
    }

    time_t               now = channel_test_mono_sec();
    channel_test_slot_t* slot = NULL;

    pthread_mutex_lock(&s_test_slots_lock);
    for (int i = 0; i < CHANNEL_TEST_MAX_SLOTS; i++) {
        if (s_test_slots[i].state == SLOT_DISPATCHED &&
            (now - s_test_slots[i].dispatched_at) >= CHANNEL_TEST_STALE_SEC) {
            s_test_slots[i].state = SLOT_FREE;
            s_test_slots[i].op = NULL;
            s_test_slots[i].ctx = NULL;
            s_test_slots[i].request_seq = 0;
            s_test_slots[i].user_id = 0;
            s_test_slots[i].rc = 0;
            s_test_slots[i].err_buf[0] = '\0';
            s_test_slots[i].dispatched_at = 0;
        }
        if (!slot && s_test_slots[i].state == SLOT_FREE) {
            slot = &s_test_slots[i];
        }
    }

    if (!slot) {
        pthread_mutex_unlock(&s_test_slots_lock);
        respond_error(c, 429, "当前通道测试请求过多，请稍后再试");
        return;
    }

    uint64_t gen = ++s_test_slot_gen;
    slot->state = SLOT_RUNNING;
    slot->generation = gen;
    slot->op = NULL;
    slot->ctx = c;
    slot->request_seq = csilk_ctx_get_request_seq(c);
    slot->user_id = user_id;
    slot->rc = -1;
    slot->err_buf[0] = '\0';
    slot->dispatched_at = 0;
    pthread_mutex_unlock(&s_test_slots_lock);

    csilk_async_op_t* op = csilk_async_op_begin(c, 0, on_channel_test_complete, NULL, NULL);
    if (!op) {
        pthread_mutex_lock(&s_test_slots_lock);
        if (slot->state == SLOT_RUNNING && slot->generation == gen) {
            slot->state = SLOT_FREE;
            slot->ctx = NULL;
            slot->request_seq = 0;
            slot->user_id = 0;
        }
        pthread_mutex_unlock(&s_test_slots_lock);
        respond_error(c, 503, "无法初始化异步测试请求");
        return;
    }

    pthread_mutex_lock(&s_test_slots_lock);
    slot->op = op;
    pthread_mutex_unlock(&s_test_slots_lock);

    if (scheduler_engine_submit_task(channel_test_worker, slot) != 0) {
        csilk_async_op_cancel(op);
        csilk_ctx_set_async(c, 0);
        pthread_mutex_lock(&s_test_slots_lock);
        if (slot->state == SLOT_RUNNING && slot->generation == gen) {
            slot->state = SLOT_FREE;
            slot->op = NULL;
            slot->ctx = NULL;
            slot->request_seq = 0;
            slot->user_id = 0;
        }
        pthread_mutex_unlock(&s_test_slots_lock);
        respond_error(c, 503, "调度引擎暂不可用，请稍后重试");
        return;
    }
}

void
register_scheduler_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/scheduler/dashboard",
                      api_scheduler_dashboard,
                      NULL,
                      NULL,
                      "Scheduler Dashboard",
                      "");
    csilk_app_get_ext(
        app, "/api/scheduler/jobs", api_scheduler_jobs_list, NULL, NULL, "List Scheduled Jobs", "");
    csilk_app_post_ext(app,
                       "/api/scheduler/jobs",
                       api_scheduler_jobs_create,
                       NULL,
                       NULL,
                       "Create Scheduled Job",
                       "");
    csilk_app_get_ext(app,
                      "/api/scheduler/jobs/:id",
                      api_scheduler_jobs_get,
                      NULL,
                      NULL,
                      "Get Scheduled Job",
                      "");
    csilk_app_put_ext(app,
                      "/api/scheduler/jobs/:id",
                      api_scheduler_jobs_update,
                      NULL,
                      NULL,
                      "Update Scheduled Job",
                      "");
    csilk_app_delete_ext(app,
                         "/api/scheduler/jobs/:id",
                         api_scheduler_jobs_delete,
                         NULL,
                         NULL,
                         "Delete Scheduled Job",
                         "");
    csilk_app_post_ext(app,
                       "/api/scheduler/jobs/:id/toggle",
                       api_scheduler_jobs_toggle,
                       NULL,
                       NULL,
                       "Toggle Job",
                       "");
    csilk_app_post_ext(app,
                       "/api/scheduler/jobs/:id/run",
                       api_scheduler_jobs_run,
                       NULL,
                       NULL,
                       "Run Job Immediately",
                       "");
    csilk_app_get_ext(app,
                      "/api/scheduler/jobs/:id/logs",
                      api_scheduler_jobs_logs,
                      NULL,
                      NULL,
                      "List Job Logs",
                      "");
    csilk_app_get_ext(app,
                      "/api/scheduler/channel",
                      api_scheduler_channel_get,
                      NULL,
                      NULL,
                      "Get Notification Channel",
                      "");
    csilk_app_put_ext(app,
                      "/api/scheduler/channel",
                      api_scheduler_channel_save,
                      NULL,
                      NULL,
                      "Save Notification Channel",
                      "");
    csilk_app_post_ext(app,
                       "/api/scheduler/channel/test",
                       api_scheduler_channel_test,
                       NULL,
                       NULL,
                       "Test Channel",
                       "");
}
