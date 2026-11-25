#include "application/ai/im_push_usecases.h"

#include "common/db.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "infrastructure/repositories/im_push_outbox_repo_impl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
push_set_err(char* err, size_t cap, const char* msg)
{
    if (err && cap > 0) {
        snprintf(err, cap, "%s", msg ? msg : "");
    }
}

static int
rollup_expired_logs(csilk_db_pool_t* db_pool, int64_t* log_ids, size_t count)
{
    if (!log_ids) {
        return 0;
    }
    int rc = 0;
    for (size_t i = 0; i < count; ++i) {
        if (log_ids[i] > 0 && mf_im_push_outbox_rollup_job_log(db_pool, log_ids[i]) != 0) {
            fprintf(stderr,
                    "[im_push] rollup expired job_run_log %lld failed\n",
                    (long long)log_ids[i]);
            rc = -1;
        }
    }
    free(log_ids);
    return rc;
}

int
im_push_usecase_enqueue(void*       pool,
                        int64_t     user_id,
                        int64_t     job_run_log_id,
                        const char* kind,
                        const char* title,
                        const char* content,
                        int64_t     ttl_sec,
                        int64_t*    out_push_id,
                        char*       err_buf,
                        size_t      err_cap)
{
    if (out_push_id) {
        *out_push_id = 0;
    }
    push_set_err(err_buf, err_cap, "");
    if (!pool || user_id <= 0) {
        push_set_err(err_buf, err_cap, "无效的用户参数");
        return -1;
    }
    if (!title || !title[0] || !content) {
        push_set_err(err_buf, err_cap, "推送标题或正文不能为空");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    inst = mf_im_bot_instance_get_by_user(db_pool, user_id, "wechat", "ilink");
    if (!inst) {
        push_set_err(err_buf,
                     err_cap,
                     "您尚未创建微信 iLink 机器人实例，请先前往「系统设置 → IM 机器人」绑定");
        return -1;
    }

    int64_t     inst_id = db_get_int(inst, "id");
    int         enabled = db_get_bool(inst, "enabled");
    const char* status = csilk_json_get_string(inst, "status");
    if (!enabled || (status && strcmp(status, "disabled") == 0)) {
        csilk_json_free(inst);
        push_set_err(err_buf, err_cap, "微信 iLink 机器人已停用，请先启用实例");
        return -1;
    }
    if (!status || strcmp(status, "active") != 0) {
        if (status && strcmp(status, "session_expired") == 0) {
            push_set_err(
                err_buf, err_cap, "微信 iLink 登录态已过期，请前往「系统设置」重新扫码登录");
        } else {
            push_set_err(err_buf,
                         err_cap,
                         "微信 iLink 机器人尚未完成扫码登录，请先前往「系统设置」完成绑定");
        }
        csilk_json_free(inst);
        return -1;
    }
    csilk_json_free(inst);

    int     max_att = (kind && strcmp(kind, "test") == 0) ? 1 : 5;
    int64_t pid = mf_im_push_outbox_enqueue(
        db_pool, inst_id, user_id, job_run_log_id, kind, title, content, ttl_sec, max_att);
    if (pid <= 0) {
        push_set_err(err_buf, err_cap, "写入推送队列失败");
        return -1;
    }
    if (out_push_id) {
        *out_push_id = pid;
    }
    return 0;
}

int
im_push_usecase_gateway_claim(void*          pool,
                              int64_t        instance_id,
                              bool           has_context,
                              const char*    context_updated_at,
                              int            limit,
                              csilk_json_t** out_items,
                              int*           code,
                              char*          err,
                              size_t         err_cap)
{
    if (out_items) {
        *out_items = NULL;
    }
    if (code) {
        *code = 0;
    }
    push_set_err(err, err_cap, "");
    if (!pool || instance_id <= 0) {
        if (code) {
            *code = 1002;
        }
        push_set_err(err, err_cap, "无效的实例 ID");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    inst = mf_im_bot_instance_get_any(db_pool, instance_id);
    if (!inst) {
        if (code) {
            *code = 1003;
        }
        push_set_err(err, err_cap, "instance not found");
        return -1;
    }
    int         enabled = db_get_bool(inst, "enabled");
    const char* status = csilk_json_get_string(inst, "status");
    bool        active = enabled && status && strcmp(status, "active") == 0;
    csilk_json_free(inst);
    if (!active) {
        if (code) {
            *code = 1004;
        }
        push_set_err(err, err_cap, "instance is not active or enabled");
        return -1;
    }

    int touch_rc = mf_im_bot_instance_touch_inbound(db_pool, instance_id, context_updated_at);
    if (touch_rc != 0) {
        fprintf(
            stderr, "[im_push] touch_inbound failed for instance %lld\n", (long long)instance_id);
        if (context_updated_at && context_updated_at[0]) {
            touch_rc = mf_im_bot_instance_touch_inbound(db_pool, instance_id, NULL);
        }
        if (touch_rc != 0) {
            if (code) {
                *code = 500;
            }
            push_set_err(err, err_cap, "更新入站状态失败");
            return -1;
        }
    }

    int64_t  now = (int64_t)time(NULL);
    int64_t* exp_logs = NULL;
    size_t   exp_cnt = 0;
    if (mf_im_push_outbox_expire_stale(db_pool, instance_id, now, NULL, &exp_logs, &exp_cnt) != 0) {
        fprintf(
            stderr, "[im_push] expire_stale failed for instance %lld\n", (long long)instance_id);
    } else if (rollup_expired_logs(db_pool, exp_logs, exp_cnt) != 0) {
        fprintf(stderr,
                "[im_push] rollup_expired_logs failed for instance %lld\n",
                (long long)instance_id);
    }

    csilk_json_t* items =
        mf_im_push_outbox_claim(db_pool, instance_id, has_context, limit, now, 60);
    if (!items) {
        if (code) {
            *code = 500;
        }
        push_set_err(err, err_cap, "领取推送任务失败");
        return -1;
    }
    if (out_items) {
        *out_items = items;
    } else {
        csilk_json_free(items);
    }
    return 0;
}

int
im_push_usecase_gateway_ack(void*       pool,
                            int64_t     instance_id,
                            int64_t     push_id,
                            const char* result,
                            const char* error_msg,
                            int         retry_after_sec,
                            int*        code,
                            char*       err,
                            size_t      err_cap)
{
    if (code) {
        *code = 0;
    }
    push_set_err(err, err_cap, "");
    if (!pool || instance_id <= 0 || push_id <= 0) {
        if (code) {
            *code = 1002;
        }
        push_set_err(err, err_cap, "参数无效");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    int64_t          log_id = 0;
    int              rc = mf_im_push_outbox_ack(db_pool,
                                   instance_id,
                                   push_id,
                                   result,
                                   error_msg,
                                   retry_after_sec,
                                   (int64_t)time(NULL),
                                   &log_id);
    if (rc == -3) {
        if (code) {
            *code = 1003;
        }
        push_set_err(err, err_cap, "push not found");
        return -1;
    }
    if (rc == -2) {
        if (code) {
            *code = 1002;
        }
        push_set_err(err, err_cap, "invalid ack result");
        return -1;
    }
    if (rc != 0) {
        if (code) {
            *code = 500;
        }
        push_set_err(err, err_cap, "更新推送状态失败");
        return -1;
    }
    if (log_id > 0 && mf_im_push_outbox_rollup_job_log(db_pool, log_id) != 0) {
        fprintf(stderr,
                "[im_push] rollup job_run_log %lld failed after ack push %lld\n",
                (long long)log_id,
                (long long)push_id);
        if (code) {
            *code = 500;
        }
        push_set_err(err, err_cap, "聚合任务推送状态失败");
        return -1;
    }
    return 0;
}

int
im_push_usecase_expire_stale(void* pool)
{
    if (!pool) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    int64_t*         exp_logs = NULL;
    size_t           exp_cnt = 0;
    int              rc =
        mf_im_push_outbox_expire_stale(db_pool, 0, (int64_t)time(NULL), NULL, &exp_logs, &exp_cnt);
    int rollup_rc = rollup_expired_logs(db_pool, exp_logs, exp_cnt);
    if (rc != 0) {
        fprintf(stderr, "[im_push] global expire_stale failed (rc=%d)\n", rc);
        return -1;
    }
    return rollup_rc != 0 ? -1 : 0;
}

csilk_json_t*
im_push_usecase_get(void* pool, int64_t push_id)
{
    return mf_im_push_outbox_get((csilk_db_pool_t*)pool, push_id);
}

int
im_push_usecase_cancel_expired_test(void* pool, int64_t push_id, const char* reason)
{
    return mf_im_push_outbox_mark_expired_by_id((csilk_db_pool_t*)pool, push_id, reason);
}

csilk_json_t*
im_push_usecase_build_ilink_status(void* pool, int64_t user_id)
{
    csilk_json_t* st = csilk_json_object();
    if (!pool || user_id <= 0) {
        csilk_json_add_bool(st, "has_instance", false);
        return st;
    }
    csilk_json_t* inst =
        mf_im_bot_instance_get_by_user((csilk_db_pool_t*)pool, user_id, "wechat", "ilink");
    if (!inst) {
        csilk_json_add_bool(st, "has_instance", false);
        return st;
    }
    csilk_json_add_bool(st, "has_instance", true);
    csilk_json_add_int(st, "instance_id", db_get_int(inst, "id"));
    const char* status = csilk_json_get_string(inst, "status");
    csilk_json_add_string(st, "status", status ? status : "");
    csilk_json_add_bool(st, "enabled", db_get_bool(inst, "enabled") != 0);
    const char* bot_id = csilk_json_get_string(inst, "ilink_bot_id");
    if (bot_id && bot_id[0]) {
        csilk_json_add_string(st, "ilink_bot_id", bot_id);
    } else {
        csilk_json_add_null(st, "ilink_bot_id");
    }
    const char* seen = csilk_json_get_string(inst, "last_seen_at");
    if (seen && seen[0]) {
        csilk_json_add_string(st, "last_seen_at", seen);
    } else {
        csilk_json_add_null(st, "last_seen_at");
    }
    const char* inb = csilk_json_get_string(inst, "last_inbound_at");
    if (inb && inb[0]) {
        csilk_json_add_string(st, "last_inbound_at", inb);
    } else {
        csilk_json_add_null(st, "last_inbound_at");
    }
    csilk_json_free(inst);
    return st;
}
