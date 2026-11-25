#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "application/ai/im_bot_instance_usecases.h"
#include "application/ai/im_push_usecases.h"
#include "application/scheduler/dtos.h"
#include "application/scheduler/usecases.h"
#include "common/db.h"
#include "common/jwt.h"
#include "config/secret.h"
#include "csilk/app/app.h"
#include "csilk/csilk.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "infrastructure/repositories/im_push_outbox_repo_impl.h"
#include "infrastructure/repositories/scheduler_repo_impl.h"
#include "interfaces/http/controllers/scheduler_controller.h"
#include "middlewares/jwt_middleware.h"

static const char* TEST_DB_PATH = "/tmp/test_mf_im_push_outbox.db";

static void
cleanup_db_files(void)
{
    unlink(TEST_DB_PATH);
    char shm[256], wal[256];
    snprintf(shm, sizeof(shm), "%s-shm", TEST_DB_PATH);
    snprintf(wal, sizeof(wal), "%s-wal", TEST_DB_PATH);
    unlink(shm);
    unlink(wal);
}

static csilk_db_pool_t*
setup_db(void)
{
    cleanup_db_files();

    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", TEST_DB_PATH);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_im_push_outbox_32b!");

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);
    return pool;
}

static int64_t
seed_user_only(csilk_db_pool_t* pool, const char* username)
{
    assert(csilk_db_exec_param(pool,
                               "INSERT INTO users (username, password) VALUES (?, 'hash')",
                               (const char*[]){username, NULL}) == 0);
    csilk_json_t* ures = csilk_db_query_param_json(
        pool, "SELECT id FROM users WHERE username=? LIMIT 1", (const char*[]){username, NULL});
    assert(ures && csilk_json_array_size(ures) == 1);
    int64_t uid = db_get_int(csilk_json_array_get(ures, 0), "id");
    csilk_json_free(ures);
    return uid;
}

static int64_t
seed_user_and_active_instance(csilk_db_pool_t* pool,
                              const char*      username,
                              const char*      bot_id,
                              const char*      wx_user)
{
    int64_t uid = seed_user_only(pool, username);
    int64_t inst_id = mf_im_bot_instance_create(pool, uid, "wechat", "ilink");
    assert(inst_id > 0);
    char err[128] = {0};
    assert(mf_im_bot_instance_activate(pool, inst_id, bot_id, wx_user, err, sizeof(err)) == 0);
    return uid;
}

static csilk_json_t*
find_log_row(csilk_db_pool_t* pool, int64_t log_id)
{
    int64_t       ltotal = 0;
    csilk_json_t* logs = mf_scheduler_repo_log_list(pool, 0, true, 0, 1, 50, &ltotal);
    assert(logs != NULL);
    csilk_json_t* found = NULL;
    for (size_t i = 0; i < csilk_json_array_size(logs); ++i) {
        csilk_json_t* row = csilk_json_array_get(logs, i);
        if (db_get_int(row, "id") == log_id) {
            found = csilk_json_copy(row);
            break;
        }
    }
    csilk_json_free(logs);
    return found;
}

static void
test_outbox_repo_lifecycle(csilk_db_pool_t* pool)
{
    int64_t u1 = seed_user_and_active_instance(pool, "push_u1", "bot_p1", "wx_p1");
    int64_t u2 = seed_user_and_active_instance(pool, "push_u2", "bot_p2", "wx_p2");

    csilk_json_t* inst1 = mf_im_bot_instance_get_by_user(pool, u1, "wechat", "ilink");
    assert(inst1 != NULL);
    int64_t inst1_id = db_get_int(inst1, "id");
    csilk_json_free(inst1);

    /* Heartbeat with last_inbound_at + touch_inbound updates metadata cleanly */
    char    st[64] = {0};
    int64_t ep = 0;
    int     en = 0;
    assert(mf_im_bot_instance_heartbeat_with_inbound(
               pool, inst1_id, "2026-11-25T10:00:00Z", st, sizeof(st), &ep, &en) == 0);
    csilk_json_t* inst1_ref = mf_im_bot_instance_get(pool, u1, inst1_id);
    assert(inst1_ref != NULL);
    assert(strcmp(csilk_json_get_string(inst1_ref, "last_inbound_at"), "2026-11-25T10:00:00Z") ==
           0);
    csilk_json_free(inst1_ref);

    /* Create job & log */
    int64_t job_id = mf_scheduler_repo_job_create(pool,
                                                  u1,
                                                  "Morning Brief",
                                                  "Desc",
                                                  "ai_prompt",
                                                  "cron",
                                                  "{\"cron\":\"0 8 * * *\"}",
                                                  "{\"prompt\":\"test\"}",
                                                  "user_default",
                                                  "{}",
                                                  "");
    assert(job_id > 0);
    int64_t log_id = mf_scheduler_repo_log_create(pool, job_id, u1, "manual_invoke");
    assert(log_id > 0);
    assert(mf_scheduler_repo_log_update_notif(pool, log_id, "queued", "") == 0);

    int64_t p_sched = mf_im_push_outbox_enqueue(
        pool, inst1_id, u1, log_id, "scheduler", "日报", "内容A", 1800, 5);
    int64_t p_test =
        mf_im_push_outbox_enqueue(pool, inst1_id, u1, 0, "test", "连通性测试", "内容Test", 30, 1);
    assert(p_sched > 0 && p_test > 0);
    int64_t now = (int64_t)time(NULL);

    /* has_context = false claims ONLY kind='test', leaving scheduler pending */
    csilk_json_t* no_ctx_items = mf_im_push_outbox_claim(pool, inst1_id, false, 5, now, 60);
    assert(no_ctx_items && csilk_json_array_size(no_ctx_items) == 1);
    assert(db_get_int(csilk_json_array_get(no_ctx_items, 0), "id") == p_test);
    csilk_json_free(no_ctx_items);

    /* Ack test with waiting_context -> fails immediately */
    int64_t ack_lid = 0;
    assert(mf_im_push_outbox_ack(
               pool, inst1_id, p_test, "waiting_context", "请先发消息", 0, now, &ack_lid) == 0);
    csilk_json_t* test_row = mf_im_push_outbox_get(pool, p_test);
    assert(test_row && strcmp(csilk_json_get_string(test_row, "status"), "failed") == 0);
    csilk_json_free(test_row);

    /* Cross-instance ack rejected (-3) */
    csilk_json_t* inst2 = mf_im_bot_instance_get_by_user(pool, u2, "wechat", "ilink");
    int64_t       inst2_id = db_get_int(inst2, "id");
    csilk_json_free(inst2);
    assert(mf_im_push_outbox_ack(pool, inst2_id, p_sched, "sent", "", 0, now, NULL) == -3);

    /* has_context = true claims scheduler row */
    csilk_json_t* items = mf_im_push_outbox_claim(pool, inst1_id, true, 5, now, 60);
    assert(items && csilk_json_array_size(items) == 1);
    assert(db_get_int(csilk_json_array_get(items, 0), "id") == p_sched);
    csilk_json_free(items);

    /* waiting_context on scheduler keeps attempts = 0 and schedules next attempt */
    assert(mf_im_push_outbox_ack(
               pool, inst1_id, p_sched, "waiting_context", "需刷新", 0, now, &ack_lid) == 0);
    assert(ack_lid == log_id);
    csilk_json_t* srow = mf_im_push_outbox_get(pool, p_sched);
    assert(srow && strcmp(csilk_json_get_string(srow, "status"), "pending") == 0);
    assert(db_get_int(srow, "attempts") == 0);
    csilk_json_free(srow);

    /* Re-claim after retry window, verify lease reclaim if now + 96 >= lease_until_ts */
    csilk_json_t* items2 = mf_im_push_outbox_claim(pool, inst1_id, true, 5, now + 35, 60);
    assert(items2 && csilk_json_array_size(items2) == 1);
    csilk_json_free(items2);

    /* Lease expires at now + 35 + 60 = now + 95; at now + 96 reclaim succeeds */
    csilk_json_t* reclaimed = mf_im_push_outbox_claim(pool, inst1_id, true, 5, now + 96, 60);
    assert(reclaimed && csilk_json_array_size(reclaimed) == 1);
    csilk_json_free(reclaimed);

    /* Ack sent and rollup job_run_logs -> sent */
    assert(mf_im_push_outbox_ack(pool, inst1_id, p_sched, "sent", "", 0, now + 97, &ack_lid) == 0);
    assert(mf_im_push_outbox_rollup_job_log(pool, log_id) == 0);

    int64_t       ltotal = 0;
    csilk_json_t* logs = mf_scheduler_repo_log_list(pool, u1, false, job_id, 1, 10, &ltotal);
    assert(logs && csilk_json_array_size(logs) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(logs, 0), "notification_status"),
                  "sent") == 0);
    csilk_json_free(logs);

    /* Mixed sync + async rollup -> partial when outbox expires */
    int64_t job2_id = mf_scheduler_repo_job_create_builtin(pool,
                                                           "Broadcast Repo Test",
                                                           "Desc",
                                                           "system_builtin",
                                                           "cron",
                                                           "{\"cron\":\"0 8 * * *\"}",
                                                           "{\"handler\":\"mcp_pool_cleanup\"}",
                                                           "user_default",
                                                           "{}",
                                                           "");
    assert(job2_id > 0);
    int64_t log2 = mf_scheduler_repo_log_create(pool, job2_id, 0, "cron_tick");
    assert(log2 > 0);
    assert(mf_scheduler_repo_log_set_sync_counts(pool, log2, 1, 0) == 0);
    int64_t p_exp =
        mf_im_push_outbox_enqueue(pool, inst2_id, u2, log2, "scheduler", "快讯", "正文", 60, 5);
    assert(p_exp > 0);
    int64_t  now2 = (int64_t)time(NULL);
    int64_t* exp_logs = NULL;
    size_t   exp_cnt = 0;
    assert(mf_im_push_outbox_expire_stale(pool, 0, now2 + 10000, "推送超时", &exp_logs, &exp_cnt) ==
           0);
    assert(exp_cnt == 1 && exp_logs[0] == log2);
    free(exp_logs);
    assert(mf_im_push_outbox_rollup_job_log(pool, log2) == 0);

    csilk_json_t* l2row = find_log_row(pool, log2);
    assert(l2row != NULL);
    assert(strcmp(csilk_json_get_string(l2row, "notification_status"), "partial") == 0);
    csilk_json_free(l2row);

    printf("  [OK] test_outbox_repo_lifecycle\n");
}

static void
test_ack_state_machine_and_expiry(csilk_db_pool_t* pool)
{
    int64_t       u = seed_user_and_active_instance(pool, "ack_u1", "bot_ack1", "wx_ack1");
    csilk_json_t* inst = mf_im_bot_instance_get_by_user(pool, u, "wechat", "ilink");
    assert(inst != NULL);
    int64_t inst_id = db_get_int(inst, "id");
    csilk_json_free(inst);

    int64_t p1 =
        mf_im_push_outbox_enqueue(pool, inst_id, u, 0, "scheduler", "重试序列", "内容", 1800, 5);
    int64_t p_cap =
        mf_im_push_outbox_enqueue(pool, inst_id, u, 0, "scheduler", "退避上限", "内容", 1800, 5);
    int64_t p_sent =
        mf_im_push_outbox_enqueue(pool, inst_id, u, 0, "scheduler", "幂等终态", "内容", 1800, 5);
    int64_t p_fail =
        mf_im_push_outbox_enqueue(pool, inst_id, u, 0, "scheduler", "直接失败", "内容", 1800, 5);
    int64_t p_exp_retry =
        mf_im_push_outbox_enqueue(pool, inst_id, u, 0, "scheduler", "超时重试", "内容", 60, 5);
    int64_t p_exp_wc =
        mf_im_push_outbox_enqueue(pool, inst_id, u, 0, "scheduler", "超时等待", "内容", 60, 5);
    assert(p1 > 0 && p_cap > 0 && p_sent > 0 && p_fail > 0 && p_exp_retry > 0 && p_exp_wc > 0);
    int64_t t0 = (int64_t)time(NULL);

    /* Invalid result -> -2 */
    assert(mf_im_push_outbox_ack(pool, inst_id, p1, "bogus", "", 0, t0, NULL) == -2);
    assert(mf_im_push_outbox_ack(pool, inst_id, p1, "", "", 0, t0, NULL) == -2);
    assert(mf_im_push_outbox_ack(pool, inst_id, p1, NULL, "", 0, t0, NULL) == -2);

    /* Default backoff sequence: 10, 30, 60, 120, then max_attempts -> failed */
    const int64_t expected_delays[] = {10, 30, 60, 120};
    int64_t       cur_t = t0;
    for (int i = 0; i < 4; ++i) {
        assert(mf_im_push_outbox_ack(pool, inst_id, p1, "retry", "err", 0, cur_t, NULL) == 0);
        csilk_json_t* row = mf_im_push_outbox_get(pool, p1);
        assert(row != NULL);
        assert(strcmp(csilk_json_get_string(row, "status"), "pending") == 0);
        assert(db_get_int(row, "attempts") == i + 1);
        assert(db_get_int(row, "next_attempt_at_ts") == cur_t + expected_delays[i]);
        csilk_json_free(row);
        cur_t += expected_delays[i];
    }

    /* 5th retry hits max_attempts = 5 -> failed */
    assert(mf_im_push_outbox_ack(pool, inst_id, p1, "retry", "exhausted", 0, cur_t, NULL) == 0);
    csilk_json_t* p1_end = mf_im_push_outbox_get(pool, p1);
    assert(p1_end != NULL);
    assert(strcmp(csilk_json_get_string(p1_end, "status"), "failed") == 0);
    assert(db_get_int(p1_end, "attempts") == 5);
    assert(strcmp(csilk_json_get_string(p1_end, "last_error"), "exhausted") == 0);
    csilk_json_free(p1_end);

    /* retry_after_sec capped at 300 */
    assert(mf_im_push_outbox_ack(pool, inst_id, p_cap, "retry", "rate_limit", 999, t0, NULL) == 0);
    csilk_json_t* cap_row = mf_im_push_outbox_get(pool, p_cap);
    assert(cap_row != NULL);
    assert(strcmp(csilk_json_get_string(cap_row, "status"), "pending") == 0);
    assert(db_get_int(cap_row, "attempts") == 1);
    assert(db_get_int(cap_row, "next_attempt_at_ts") == t0 + 300);
    csilk_json_free(cap_row);

    /* Direct failed result */
    assert(mf_im_push_outbox_ack(pool, inst_id, p_fail, "failed", "网关拒绝", 0, t0, NULL) == 0);
    csilk_json_t* fail_row = mf_im_push_outbox_get(pool, p_fail);
    assert(fail_row != NULL);
    assert(strcmp(csilk_json_get_string(fail_row, "status"), "failed") == 0);
    assert(strcmp(csilk_json_get_string(fail_row, "last_error"), "网关拒绝") == 0);
    csilk_json_free(fail_row);

    /* Terminal-state idempotency: ack on sent row returns 0 and leaves sent untouched */
    assert(mf_im_push_outbox_ack(pool, inst_id, p_sent, "sent", "", 0, t0, NULL) == 0);
    assert(mf_im_push_outbox_ack(pool, inst_id, p_sent, "failed", "late err", 0, t0 + 1, NULL) ==
           0);
    assert(mf_im_push_outbox_ack(pool, inst_id, p_sent, "retry", "late retry", 0, t0 + 2, NULL) ==
           0);
    csilk_json_t* sent_row = mf_im_push_outbox_get(pool, p_sent);
    assert(sent_row != NULL);
    assert(strcmp(csilk_json_get_string(sent_row, "status"), "sent") == 0);
    assert(db_get_int(sent_row, "attempts") == 0);
    csilk_json_free(sent_row);

    /* expires_at <= now on retry -> expired */
    assert(mf_im_push_outbox_ack(
               pool, inst_id, p_exp_retry, "retry", "expired_retry", 0, t0 + 120, NULL) == 0);
    csilk_json_t* er_row = mf_im_push_outbox_get(pool, p_exp_retry);
    assert(er_row != NULL);
    assert(strcmp(csilk_json_get_string(er_row, "status"), "expired") == 0);
    assert(db_get_int(er_row, "attempts") == 1);
    csilk_json_free(er_row);

    /* expires_at <= now on waiting_context -> expired */
    assert(mf_im_push_outbox_ack(
               pool, inst_id, p_exp_wc, "waiting_context", "", 0, t0 + 120, NULL) == 0);
    csilk_json_t* ew_row = mf_im_push_outbox_get(pool, p_exp_wc);
    assert(ew_row != NULL);
    assert(strcmp(csilk_json_get_string(ew_row, "status"), "expired") == 0);
    assert(db_get_int(ew_row, "attempts") == 0);
    csilk_json_free(ew_row);

    printf("  [OK] test_ack_state_machine_and_expiry\n");
}

static void
test_mark_expired_by_id(csilk_db_pool_t* pool)
{
    int64_t       u = seed_user_and_active_instance(pool, "exp_u1", "bot_exp1", "wx_exp1");
    csilk_json_t* inst = mf_im_bot_instance_get_by_user(pool, u, "wechat", "ilink");
    assert(inst != NULL);
    int64_t inst_id = db_get_int(inst, "id");
    csilk_json_free(inst);

    int64_t p_pend = mf_im_push_outbox_enqueue(pool, inst_id, u, 0, "test", "等待", "c1", 60, 1);
    int64_t p_done = mf_im_push_outbox_enqueue(pool, inst_id, u, 0, "test", "完成", "c2", 60, 1);
    assert(p_pend > 0 && p_done > 0);
    int64_t now = (int64_t)time(NULL);

    assert(mf_im_push_outbox_ack(pool, inst_id, p_done, "sent", "", 0, now, NULL) == 0);
    assert(mf_im_push_outbox_mark_expired_by_id(pool, p_pend, "测试超时取消") == 0);
    assert(mf_im_push_outbox_mark_expired_by_id(pool, p_done, "不应覆盖终态") == 0);

    csilk_json_t* r_pend = mf_im_push_outbox_get(pool, p_pend);
    assert(r_pend != NULL);
    assert(strcmp(csilk_json_get_string(r_pend, "status"), "expired") == 0);
    assert(db_get_int(r_pend, "lease_until_ts") == 0);
    assert(strcmp(csilk_json_get_string(r_pend, "last_error"), "测试超时取消") == 0);
    csilk_json_free(r_pend);

    csilk_json_t* r_done = mf_im_push_outbox_get(pool, p_done);
    assert(r_done != NULL);
    assert(strcmp(csilk_json_get_string(r_done, "status"), "sent") == 0);
    csilk_json_free(r_done);

    printf("  [OK] test_mark_expired_by_id\n");
}

static void
test_inbound_validation_and_activate_switch(csilk_db_pool_t* pool)
{
    int64_t       u = seed_user_and_active_instance(pool, "inb_u1", "bot_inb1", "wx_inb1");
    csilk_json_t* inst = mf_im_bot_instance_get_by_user(pool, u, "wechat", "ilink");
    assert(inst != NULL);
    int64_t inst_id = db_get_int(inst, "id");
    csilk_json_free(inst);

    assert(mf_im_bot_instance_touch_inbound(pool, inst_id, "2026-11-25T10:00:00Z") == 0);

    /* Reject invalid ISO strings cleanly without altering last_inbound_at */
    static const char* kInvalidTs[] = {
        "2026-11-25garbage0000",
        "2026-13-25T10:00:00Z",
        "2026-11-32T10:00:00Z",
        "2026-11-25T24:00:00Z",
        "2026-11-25T10:00:00.Z",
        "2026-11-25T10:00:00Zjunk",
        "2026-11-25T10:00:00+08:99",
        "2026-11-25T10:00:00+080",
        "",
    };
    for (size_t i = 0; i < sizeof(kInvalidTs) / sizeof(kInvalidTs[0]); ++i) {
        assert(mf_im_bot_instance_touch_inbound(pool, inst_id, kInvalidTs[i]) == 0);
        csilk_json_t* cur = mf_im_bot_instance_get(pool, u, inst_id);
        assert(cur != NULL);
        assert(strcmp(csilk_json_get_string(cur, "last_inbound_at"), "2026-11-25T10:00:00Z") == 0);
        csilk_json_free(cur);
    }

    /* Accept valid ISO variants */
    static const char* kValidTs[] = {
        "2026-11-25 11:22:33.123456+08:00",
        "2026-11-25T12:34:56-0500",
        "2026-11-25T13:00:00+08",
    };
    for (size_t i = 0; i < sizeof(kValidTs) / sizeof(kValidTs[0]); ++i) {
        assert(mf_im_bot_instance_touch_inbound(pool, inst_id, kValidTs[i]) == 0);
        csilk_json_t* cur = mf_im_bot_instance_get(pool, u, inst_id);
        assert(cur != NULL);
        assert(strcmp(csilk_json_get_string(cur, "last_inbound_at"), kValidTs[i]) == 0);
        csilk_json_free(cur);
    }

    /* Re-activating same WeChat user preserves last_inbound_at */
    char err[128] = {0};
    assert(mf_im_bot_instance_activate(pool, inst_id, "bot_inb1", "wx_inb1", err, sizeof(err)) ==
           0);
    csilk_json_t* same_ref = mf_im_bot_instance_get(pool, u, inst_id);
    assert(same_ref != NULL);
    assert(strcmp(csilk_json_get_string(same_ref, "last_inbound_at"), "2026-11-25T13:00:00+08") ==
           0);
    csilk_json_free(same_ref);

    /* Switching WeChat user on re-login clears stale last_inbound_at */
    assert(mf_im_bot_instance_activate(pool, inst_id, "bot_inb2", "wx_inb2", err, sizeof(err)) ==
           0);
    csilk_json_t* sw_ref = mf_im_bot_instance_get(pool, u, inst_id);
    assert(sw_ref != NULL);
    const char* cleared = csilk_json_get_string(sw_ref, "last_inbound_at");
    assert(cleared == NULL || cleared[0] == '\0');
    csilk_json_free(sw_ref);

    /* Unknown instance heartbeat returns -3 */
    char    st[32] = {0};
    int64_t ep = 0;
    int     en = 0;
    assert(mf_im_bot_instance_heartbeat_with_inbound(
               pool, 9999999, "2026-11-25T10:00:00Z", st, sizeof(st), &ep, &en) == -3);

    printf("  [OK] test_inbound_validation_and_activate_switch\n");
}

static void
test_list_active_channels(csilk_db_pool_t* pool)
{
    int64_t ch_u1 = seed_user_only(pool, "ch_u1");
    int64_t ch_u2 = seed_user_only(pool, "ch_u2");
    int64_t ch_u3 = seed_user_only(pool, "ch_u3");
    int64_t ch_u4 = seed_user_only(pool, "ch_u4");
    assert(ch_u1 < ch_u2 && ch_u2 < ch_u3 && ch_u3 < ch_u4);

    assert(mf_scheduler_repo_save_user_channel(pool, ch_u3, "webhook", "Inact", "https://ex", "") ==
           0);
    char u3_s[32];
    snprintf(u3_s, sizeof(u3_s), "%lld", (long long)ch_u3);
    assert(
        csilk_db_exec_param(pool,
                            "UPDATE user_notification_channels SET is_active = 0 WHERE user_id = ?",
                            (const char*[]){u3_s, NULL}) == 0);
    assert(mf_scheduler_repo_save_user_channel(pool, ch_u4, "none", "Disabled", "", "") == 0);
    assert(mf_scheduler_repo_save_user_channel(pool, ch_u2, "wechat_ilink", "Wx2", "", "") == 0);
    assert(mf_scheduler_repo_save_user_channel(pool, ch_u1, "serverchan", "SC1", "", "k1") == 0);

    csilk_json_t* chs = mf_scheduler_repo_list_active_channels(pool);
    assert(chs != NULL && csilk_json_is_array(chs));
    assert(csilk_json_array_size(chs) == 2);
    csilk_json_t* r0 = csilk_json_array_get(chs, 0);
    csilk_json_t* r1 = csilk_json_array_get(chs, 1);
    assert(db_get_int(r0, "user_id") == ch_u1);
    assert(strcmp(csilk_json_get_string(r0, "channel_type"), "serverchan") == 0);
    assert(db_get_int(r1, "user_id") == ch_u2);
    assert(strcmp(csilk_json_get_string(r1, "channel_type"), "wechat_ilink") == 0);
    csilk_json_free(chs);

    printf("  [OK] test_list_active_channels\n");
}

static void
test_rollup_variants(csilk_db_pool_t* pool)
{
    int64_t       u = seed_user_and_active_instance(pool, "roll_u", "bot_roll", "wx_roll");
    csilk_json_t* inst = mf_im_bot_instance_get_by_user(pool, u, "wechat", "ilink");
    assert(inst != NULL);
    int64_t inst_id = db_get_int(inst, "id");
    csilk_json_free(inst);

    int64_t job_id = mf_scheduler_repo_job_create(pool,
                                                  u,
                                                  "Rollup Job",
                                                  "Desc",
                                                  "ai_prompt",
                                                  "cron",
                                                  "{\"cron\":\"0 9 * * *\"}",
                                                  "{\"prompt\":\"test\"}",
                                                  "user_default",
                                                  "{}",
                                                  "");
    assert(job_id > 0);
    int64_t now = (int64_t)time(NULL);

    /* Pure failed -> notification_status = failed + error_message populated */
    int64_t log_a = mf_scheduler_repo_log_create(pool, job_id, u, "cron_tick");
    assert(log_a > 0);
    int64_t pa =
        mf_im_push_outbox_enqueue(pool, inst_id, u, log_a, "scheduler", "A", "body", 1800, 5);
    assert(pa > 0);
    assert(mf_im_push_outbox_ack(pool, inst_id, pa, "failed", "发送失败A", 0, now, NULL) == 0);
    assert(mf_im_push_outbox_rollup_job_log(pool, log_a) == 0);
    csilk_json_t* ra = find_log_row(pool, log_a);
    assert(ra != NULL);
    assert(strcmp(csilk_json_get_string(ra, "notification_status"), "failed") == 0);
    assert(strcmp(csilk_json_get_string(ra, "error_message"), "发送失败A") == 0);
    csilk_json_free(ra);

    /* Mixed pending + sent -> stays queued */
    int64_t log_b = mf_scheduler_repo_log_create(pool, job_id, u, "cron_tick");
    assert(log_b > 0);
    assert(mf_scheduler_repo_log_set_sync_counts(pool, log_b, 1, 0) == 0);
    int64_t pb1 =
        mf_im_push_outbox_enqueue(pool, inst_id, u, log_b, "scheduler", "B1", "body", 1800, 5);
    int64_t pb2 =
        mf_im_push_outbox_enqueue(pool, inst_id, u, log_b, "scheduler", "B2", "body", 1800, 5);
    assert(pb1 > 0 && pb2 > 0);
    assert(mf_im_push_outbox_ack(pool, inst_id, pb1, "sent", "", 0, now, NULL) == 0);
    assert(mf_im_push_outbox_rollup_job_log(pool, log_b) == 0);
    csilk_json_t* rb = find_log_row(pool, log_b);
    assert(rb != NULL);
    assert(strcmp(csilk_json_get_string(rb, "notification_status"), "queued") == 0);
    csilk_json_free(rb);

    /* Clearing error_message when fail_total == 0 && out_pending == 0 */
    int64_t log_c = mf_scheduler_repo_log_create(pool, job_id, u, "cron_tick");
    assert(log_c > 0);
    assert(mf_scheduler_repo_log_update_notif(pool, log_c, "queued", "旧等待错误") == 0);
    int64_t pc =
        mf_im_push_outbox_enqueue(pool, inst_id, u, log_c, "scheduler", "C", "body", 1800, 5);
    assert(pc > 0);
    assert(mf_im_push_outbox_ack(pool, inst_id, pc, "sent", "", 0, now, NULL) == 0);
    assert(mf_im_push_outbox_rollup_job_log(pool, log_c) == 0);
    csilk_json_t* rc = find_log_row(pool, log_c);
    assert(rc != NULL);
    assert(strcmp(csilk_json_get_string(rc, "notification_status"), "sent") == 0);
    const char* err_c = csilk_json_get_string(rc, "error_message");
    assert(err_c == NULL || err_c[0] == '\0');
    csilk_json_free(rc);

    /* Negative sync counts clamped to 0 */
    int64_t log_d = mf_scheduler_repo_log_create(pool, job_id, u, "cron_tick");
    assert(log_d > 0);
    assert(mf_scheduler_repo_log_set_sync_counts(pool, log_d, -5, -2) == 0);
    assert(mf_im_push_outbox_rollup_job_log(pool, log_d) == 0);
    csilk_json_t* rd = find_log_row(pool, log_d);
    assert(rd != NULL);
    assert(db_get_int(rd, "notif_sync_sent") == 0);
    assert(db_get_int(rd, "notif_sync_failed") == 0);
    assert(strcmp(csilk_json_get_string(rd, "notification_status"), "none") == 0);
    csilk_json_free(rd);

    printf("  [OK] test_rollup_variants\n");
}

static void
test_push_usecases(csilk_db_pool_t* pool)
{
    int64_t u3 = seed_user_and_active_instance(pool, "push_u3", "bot_p3", "wx_p3");
    int64_t u4 = seed_user_and_active_instance(pool, "push_u4", "bot_p4", "wx_p4");
    int64_t push_id = 0;
    char    err[256] = {0};

    /* Empty/NULL title or NULL content rejected upfront */
    assert(im_push_usecase_enqueue(
               pool, u3, 0, "scheduler", NULL, "C", 1800, &push_id, err, sizeof(err)) != 0);
    assert(strstr(err, "不能为空") != NULL);
    assert(im_push_usecase_enqueue(
               pool, u3, 0, "scheduler", "", "C", 1800, &push_id, err, sizeof(err)) != 0);
    assert(strstr(err, "不能为空") != NULL);
    assert(im_push_usecase_enqueue(
               pool, u3, 0, "scheduler", "T", NULL, 1800, &push_id, err, sizeof(err)) != 0);
    assert(strstr(err, "不能为空") != NULL);

    /* User without instance cannot enqueue */
    assert(im_push_usecase_enqueue(
               pool, 99999, 0, "scheduler", "T", "C", 1800, &push_id, err, sizeof(err)) != 0);
    assert(strstr(err, "未绑定") != NULL || strstr(err, "创建") != NULL);

    /* build_ilink_status when user has no instance */
    csilk_json_t* st_none = im_push_usecase_build_ilink_status(pool, 99999);
    assert(st_none && db_get_bool(st_none, "has_instance") == 0);
    csilk_json_free(st_none);

    /* Pending login & session_expired instance checks */
    int64_t u_pend = seed_user_only(pool, "push_upend");
    int64_t pend_inst_id = mf_im_bot_instance_create(pool, u_pend, "wechat", "ilink");
    assert(pend_inst_id > 0);
    assert(im_push_usecase_enqueue(
               pool, u_pend, 0, "scheduler", "T", "C", 1800, &push_id, err, sizeof(err)) != 0);
    assert(strstr(err, "扫码") != NULL);
    csilk_json_t* items = NULL;
    int           code = 0;
    assert(im_push_usecase_gateway_claim(
               pool, pend_inst_id, true, "", 5, &items, &code, err, sizeof(err)) != 0 &&
           code == 1004);

    assert(mf_im_bot_instance_update_status(pool, pend_inst_id, "session_expired", "", "", "exp") ==
           0);
    assert(im_push_usecase_enqueue(
               pool, u_pend, 0, "scheduler", "T", "C", 1800, &push_id, err, sizeof(err)) != 0);
    assert(strstr(err, "过期") != NULL);

    /* Create scheduler job & log for rollup tests */
    int64_t job_id = mf_scheduler_repo_job_create(pool,
                                                  u3,
                                                  "Usecase Rollup Job",
                                                  "Desc",
                                                  "ai_prompt",
                                                  "cron",
                                                  "{\"cron\":\"0 8 * * *\"}",
                                                  "{\"prompt\":\"test\"}",
                                                  "user_default",
                                                  "{}",
                                                  "");
    assert(job_id > 0);
    int64_t log_sent = mf_scheduler_repo_log_create(pool, job_id, u3, "manual_invoke");
    assert(log_sent > 0);
    assert(mf_scheduler_repo_log_update_notif(pool, log_sent, "queued", "") == 0);

    /* Enqueue for active user u3 with job_run_log_id > 0 */
    assert(
        im_push_usecase_enqueue(
            pool, u3, log_sent, "scheduler", "U3 推送", "正文", 1800, &push_id, err, sizeof(err)) ==
        0);
    assert(push_id > 0);

    csilk_json_t* st = im_push_usecase_build_ilink_status(pool, u3);
    assert(st && db_get_bool(st, "has_instance") == 1);
    int64_t inst3_id = db_get_int(st, "instance_id");
    assert(strcmp(csilk_json_get_string(st, "status"), "active") == 0);
    assert(db_get_bool(st, "enabled") == 1);
    assert(strcmp(csilk_json_get_string(st, "ilink_bot_id"), "bot_p3") == 0);
    csilk_json_free(st);

    csilk_json_t* st4 = im_push_usecase_build_ilink_status(pool, u4);
    assert(st4 && db_get_bool(st4, "has_instance") == 1);
    int64_t inst4_id = db_get_int(st4, "instance_id");
    csilk_json_free(st4);

    /* instance_id <= 0 -> 1002; non-existent instance claim -> 1003 */
    assert(im_push_usecase_gateway_claim(pool, 0, true, "", 5, &items, &code, err, sizeof(err)) !=
               0 &&
           code == 1002);
    assert(im_push_usecase_gateway_claim(
               pool, 99999, true, "", 5, &items, &code, err, sizeof(err)) != 0 &&
           code == 1003);

    /* Claim persists context_updated_at -> last_inbound_at */
    assert(im_push_usecase_gateway_claim(
               pool, inst3_id, true, "2026-11-25T11:22:33Z", 5, &items, &code, err, sizeof(err)) ==
           0);
    assert(items && csilk_json_array_size(items) == 1);
    csilk_json_free(items);

    csilk_json_t* st_after_claim = im_push_usecase_build_ilink_status(pool, u3);
    assert(st_after_claim && strcmp(csilk_json_get_string(st_after_claim, "last_inbound_at"),
                                    "2026-11-25T11:22:33Z") == 0);
    csilk_json_free(st_after_claim);

    /* Invalid ack result -> 1002, unknown push -> 1003, cross-instance ack -> 1003 */
    assert(im_push_usecase_gateway_ack(
               pool, inst3_id, push_id, "bogus", "", 0, &code, err, sizeof(err)) != 0 &&
           code == 1002);
    assert(im_push_usecase_gateway_ack(
               pool, inst3_id, 999999, "sent", "", 0, &code, err, sizeof(err)) != 0 &&
           code == 1003);
    assert(im_push_usecase_gateway_ack(
               pool, inst4_id, push_id, "sent", "", 0, &code, err, sizeof(err)) != 0 &&
           code == 1003);

    /* Ack sent updates job_run_log notification_status to 'sent', global expire leaves sent intact */
    assert(im_push_usecase_gateway_ack(
               pool, inst3_id, push_id, "sent", "", 0, &code, err, sizeof(err)) == 0);
    csilk_json_t* log_sent_row = find_log_row(pool, log_sent);
    assert(log_sent_row &&
           strcmp(csilk_json_get_string(log_sent_row, "notification_status"), "sent") == 0);
    csilk_json_free(log_sent_row);

    assert(im_push_usecase_expire_stale(pool) == 0);
    csilk_json_t* sent_item = im_push_usecase_get(pool, push_id);
    assert(sent_item && strcmp(csilk_json_get_string(sent_item, "status"), "sent") == 0);
    csilk_json_free(sent_item);

    /* Global stale expiry expires overdue row and rolls up its log to 'failed' */
    int64_t log_exp = mf_scheduler_repo_log_create(pool, job_id, u3, "cron_tick");
    assert(log_exp > 0);
    assert(mf_scheduler_repo_log_update_notif(pool, log_exp, "queued", "") == 0);
    int64_t exp_pid = 0;
    assert(
        im_push_usecase_enqueue(
            pool, u3, log_exp, "scheduler", "过期测试", "正文", 60, &exp_pid, err, sizeof(err)) ==
        0);
    char exp_pid_s[32];
    snprintf(exp_pid_s, sizeof(exp_pid_s), "%lld", (long long)exp_pid);
    assert(csilk_db_exec_param(pool,
                               "UPDATE im_push_outbox SET expires_at_ts=1 WHERE id=?",
                               (const char*[]){exp_pid_s, NULL}) == 0);
    assert(im_push_usecase_expire_stale(pool) == 0);
    csilk_json_t* exp_row = im_push_usecase_get(pool, exp_pid);
    assert(exp_row && strcmp(csilk_json_get_string(exp_row, "status"), "expired") == 0);
    csilk_json_free(exp_row);
    csilk_json_t* log_exp_row = find_log_row(pool, log_exp);
    assert(log_exp_row &&
           strcmp(csilk_json_get_string(log_exp_row, "notification_status"), "failed") == 0);
    csilk_json_free(log_exp_row);

    /* Enqueue test push & cancel expired test */
    int64_t test_push_id = 0;
    assert(im_push_usecase_enqueue(
               pool, u3, 0, "test", "Test", "Ping", 30, &test_push_id, err, sizeof(err)) == 0);
    assert(test_push_id > 0);
    assert(im_push_usecase_cancel_expired_test(pool, test_push_id, "等待超时") == 0);
    csilk_json_t* canceled = im_push_usecase_get(pool, test_push_id);
    assert(canceled && strcmp(csilk_json_get_string(canceled, "status"), "expired") == 0);
    csilk_json_free(canceled);

    /* Heartbeat ext: non-existent 1003, active updates last_inbound_at, disabled 1004 */
    csilk_json_t* hb = NULL;
    assert(im_instance_gateway_heartbeat_ext(
               pool, 99999, "2026-11-25T12:00:00Z", &hb, &code, err, sizeof(err)) != 0 &&
           code == 1003);
    assert(im_instance_gateway_heartbeat_ext(
               pool, inst3_id, "2026-11-25T12:00:00Z", &hb, &code, err, sizeof(err)) == 0);
    assert(hb != NULL);
    assert(strcmp(csilk_json_get_string(hb, "status"), "active") == 0);
    csilk_json_free(hb);

    csilk_json_t* st2 = im_push_usecase_build_ilink_status(pool, u3);
    assert(st2 &&
           strcmp(csilk_json_get_string(st2, "last_inbound_at"), "2026-11-25T12:00:00Z") == 0);
    csilk_json_free(st2);

    /* Disabled instance blocks enqueue, gateway_claim (1004), and heartbeat_ext (1004) */
    assert(mf_im_bot_instance_set_enabled(pool, u3, inst3_id, 0) == 0);
    assert(im_push_usecase_enqueue(
               pool, u3, 0, "scheduler", "T2", "C2", 1800, &push_id, err, sizeof(err)) != 0);
    assert(strstr(err, "停用") != NULL);
    assert(im_push_usecase_gateway_claim(
               pool, inst3_id, true, "", 5, &items, &code, err, sizeof(err)) != 0 &&
           code == 1004);
    assert(im_instance_gateway_heartbeat_ext(
               pool, inst3_id, "2026-11-25T12:05:00Z", &hb, &code, err, sizeof(err)) != 0 &&
           code == 1004);

    printf("  [OK] test_push_usecases\n");
}

typedef struct {
    csilk_db_pool_t* pool;
    int64_t          inst_id;
    const char*      ack_result;
    const char*      ack_err;
} test_worker_ctx_t;

static void*
mock_ilink_test_ack_worker(void* arg)
{
    test_worker_ctx_t* w = (test_worker_ctx_t*)arg;
    const char*        res = w->ack_result ? w->ack_result : "sent";
    const char*        aerr = w->ack_err ? w->ack_err : "";
    for (int i = 0; i < 40; ++i) {
        csilk_json_t* items = NULL;
        int           code = 0;
        char          err[128] = {0};
        if (im_push_usecase_gateway_claim(w->pool,
                                          w->inst_id,
                                          true,
                                          "2026-11-25T13:00:00Z",
                                          5,
                                          &items,
                                          &code,
                                          err,
                                          sizeof(err)) == 0 &&
            items && csilk_json_array_size(items) > 0) {
            csilk_json_t* row = csilk_json_array_get(items, 0);
            int64_t       pid = db_get_int(row, "id");
            assert(im_push_usecase_gateway_ack(
                       w->pool, w->inst_id, pid, res, aerr, 0, &code, err, sizeof(err)) == 0);
            csilk_json_free(items);
            return NULL;
        }
        if (items) {
            csilk_json_free(items);
        }
        struct timespec req = {0, 25000000L}; /* 25ms */
        nanosleep(&req, NULL);
    }
    return NULL;
}

static void
test_scheduler_ilink_channel(csilk_db_pool_t* pool)
{
    int64_t       u = seed_user_and_active_instance(pool, "push_sched_u1", "bot_s1", "wx_s1");
    csilk_json_t* inst = mf_im_bot_instance_get_by_user(pool, u, "wechat", "ilink");
    assert(inst != NULL);
    int64_t inst_id = db_get_int(inst, "id");
    csilk_json_free(inst);

    channel_save_dto_t dto;
    memset(&dto, 0, sizeof(dto));

    /* Invalid channel_type -> 1002 */
    snprintf(dto.channel_type, sizeof(dto.channel_type), "bogus");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 1002);
    dto.channel_type[0] = '\0';
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 1002);

    /* Legacy aliases & 'none' accepted; 'none' clears webhook/secret */
    snprintf(dto.channel_type, sizeof(dto.channel_type), "wechatwork");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "企业微信1");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://qyapi.weixin.qq.com/x");
    snprintf(dto.secret_key, sizeof(dto.secret_key), "SEC123456");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    snprintf(dto.channel_type, sizeof(dto.channel_type), "qywx");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    snprintf(dto.channel_type, sizeof(dto.channel_type), "none");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "停用");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);
    csilk_json_t* none_resp = NULL;
    assert(scheduler_usecase_get_channel(pool, u, &none_resp) == 0 && none_resp != NULL);
    assert(strcmp(csilk_json_get_string(none_resp, "channel_type"), "none") == 0);
    assert(strcmp(csilk_json_get_string(none_resp, "webhook_url"), "") == 0);
    assert(strcmp(csilk_json_get_string(none_resp, "secret_key_masked"), "") == 0);
    csilk_json_free(none_resp);
    char err[256] = {0};
    assert(scheduler_usecase_test_channel(pool, u, err, sizeof(err)) != 0);

    /* wechat_ilink clears webhook/secret cleanly */
    snprintf(dto.channel_type, sizeof(dto.channel_type), "wechat_ilink");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "个人微信 iLink");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://should.clear");
    snprintf(dto.secret_key, sizeof(dto.secret_key), "SHOULD_CLEAR");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    csilk_json_t* ch_resp = NULL;
    assert(scheduler_usecase_get_channel(pool, u, &ch_resp) == 0 && ch_resp != NULL);
    assert(strcmp(csilk_json_get_string(ch_resp, "channel_type"), "wechat_ilink") == 0);
    assert(strcmp(csilk_json_get_string(ch_resp, "webhook_url"), "") == 0);
    assert(strcmp(csilk_json_get_string(ch_resp, "secret_key_masked"), "") == 0);
    csilk_json_t* ilink_st = csilk_json_get(ch_resp, "ilink_status");
    assert(ilink_st != NULL && db_get_bool(ilink_st, "has_instance"));
    csilk_json_free(ch_resp);

    csilk_json_t* dash_resp = NULL;
    assert(scheduler_usecase_dashboard(pool, u, false, &dash_resp) == 0 && dash_resp != NULL);
    csilk_json_t* dash_ch = csilk_json_get(dash_resp, "push_channel");
    assert(dash_ch && csilk_json_get(dash_ch, "ilink_status") != NULL);
    csilk_json_free(dash_resp);

    /* Closed-loop test succeeds when gateway acks inside window */
    test_worker_ctx_t ctx = {.pool = pool, .inst_id = inst_id, .ack_result = "sent"};
    pthread_t         tid;
    assert(pthread_create(&tid, NULL, mock_ilink_test_ack_worker, &ctx) == 0);
    assert(scheduler_usecase_test_channel(pool, u, err, sizeof(err)) == 0);
    assert(err[0] == '\0');
    pthread_join(tid, NULL);

    /* Gateway acks 'failed' -> propagates last_error cleanly */
    test_worker_ctx_t fctx = {
        .pool = pool, .inst_id = inst_id, .ack_result = "failed", .ack_err = "网关投递被拒"};
    assert(pthread_create(&tid, NULL, mock_ilink_test_ack_worker, &fctx) == 0);
    assert(scheduler_usecase_test_channel(pool, u, err, sizeof(err)) != 0);
    assert(strcmp(err, "网关投递被拒") == 0);
    pthread_join(tid, NULL);

    /* Gateway offline -> times out inside short test window and expires outbox row */
    setenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS", "300", 1);
    assert(scheduler_usecase_test_channel(pool, u, err, sizeof(err)) != 0);
    assert(strstr(err, "响应") != NULL);

    char uid_s[32];
    snprintf(uid_s, sizeof(uid_s), "%lld", (long long)u);
    csilk_json_t* last_t = csilk_db_query_param_json(
        pool,
        "SELECT id FROM im_push_outbox WHERE mf_user_id=? AND kind='test' ORDER BY id DESC LIMIT 1",
        (const char*[]){uid_s, NULL});
    assert(last_t && csilk_json_array_size(last_t) == 1);
    int64_t timed_out_pid = db_get_int(csilk_json_array_get(last_t, 0), "id");
    csilk_json_free(last_t);
    csilk_json_t* exp_row = im_push_usecase_get(pool, timed_out_pid);
    assert(exp_row && strcmp(csilk_json_get_string(exp_row, "status"), "expired") == 0);
    csilk_json_free(exp_row);

    /* NULL / 0 err_buf never crashes across error & timeout paths */
    assert(scheduler_usecase_test_channel(NULL, u, NULL, 0) != 0);
    assert(scheduler_usecase_test_channel(pool, 99999, NULL, 0) != 0);
    assert(scheduler_usecase_test_channel(pool, u, NULL, 0) != 0);
    unsetenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS");

    printf("  [OK] test_scheduler_ilink_channel\n");
}

typedef struct {
    int remaining_alive_calls;
    int call_count;
} cancel_probe_ctx_t;

static int
test_cancel_probe_is_alive(void* ctx)
{
    cancel_probe_ctx_t* p = (cancel_probe_ctx_t*)ctx;
    p->call_count++;
    if (p->remaining_alive_calls > 0) {
        p->remaining_alive_calls--;
        return 1;
    }
    return 0;
}

static void
test_scheduler_ilink_channel_cancellable(csilk_db_pool_t* pool)
{
    int64_t u = seed_user_and_active_instance(pool, "cancel_u1", "bot_cancel1", "wx_cancel1");
    channel_save_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "wechat_ilink");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "Cancel Probe iLink");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    setenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS", "10000", 1);

    /* Case 1: already dead on entry -> returns promptly without enqueueing */
    cancel_probe_ctx_t probe0 = {.remaining_alive_calls = 0, .call_count = 0};
    char               err[256] = {0};
    assert(scheduler_usecase_test_channel_cancellable(
               pool, u, test_cancel_probe_is_alive, &probe0, err, sizeof(err)) != 0);
    assert(strcmp(err, "测试请求已取消") == 0);
    assert(probe0.call_count == 1);

    char uid_s[32];
    snprintf(uid_s, sizeof(uid_s), "%lld", (long long)u);
    csilk_json_t* pre_rows = csilk_db_query_param_json(
        pool,
        "SELECT id FROM im_push_outbox WHERE mf_user_id=? AND kind='test'",
        (const char*[]){uid_s, NULL});
    assert(pre_rows && csilk_json_array_size(pre_rows) == 0);
    csilk_json_free(pre_rows);

    /* Case 2: alive at entry, cancelled on first poll -> expires queued row promptly */
    cancel_probe_ctx_t probe1 = {.remaining_alive_calls = 1, .call_count = 0};
    struct timespec    t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    assert(scheduler_usecase_test_channel_cancellable(
               pool, u, test_cancel_probe_is_alive, &probe1, err, sizeof(err)) != 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000L + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
    assert(strcmp(err, "测试请求已取消") == 0);
    assert(elapsed_ms < 1000);
    assert(probe1.call_count == 2);

    csilk_json_t* rows1 = csilk_db_query_param_json(
        pool,
        "SELECT id FROM im_push_outbox WHERE mf_user_id=? AND kind='test' ORDER BY id DESC LIMIT 1",
        (const char*[]){uid_s, NULL});
    assert(rows1 && csilk_json_array_size(rows1) == 1);
    int64_t pid1 = db_get_int(csilk_json_array_get(rows1, 0), "id");
    csilk_json_free(rows1);
    csilk_json_t* exp1 = im_push_usecase_get(pool, pid1);
    assert(exp1 && strcmp(csilk_json_get_string(exp1, "status"), "expired") == 0);
    assert(strcmp(csilk_json_get_string(exp1, "last_error"), "测试请求已取消") == 0);
    csilk_json_free(exp1);

    /* Case 3: alive through first poll, cancelled after 100ms sleep on second poll */
    cancel_probe_ctx_t probe2 = {.remaining_alive_calls = 2, .call_count = 0};
    clock_gettime(CLOCK_MONOTONIC, &t0);
    assert(scheduler_usecase_test_channel_cancellable(
               pool, u, test_cancel_probe_is_alive, &probe2, err, sizeof(err)) != 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000L + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
    assert(strcmp(err, "测试请求已取消") == 0);
    assert(elapsed_ms >= 90 && elapsed_ms < 1000);
    assert(probe2.call_count == 3);

    csilk_json_t* rows2 = csilk_db_query_param_json(
        pool,
        "SELECT id FROM im_push_outbox WHERE mf_user_id=? AND kind='test' ORDER BY id DESC LIMIT 1",
        (const char*[]){uid_s, NULL});
    assert(rows2 && csilk_json_array_size(rows2) == 1);
    int64_t pid2 = db_get_int(csilk_json_array_get(rows2, 0), "id");
    csilk_json_free(rows2);
    assert(pid2 > pid1);
    csilk_json_t* exp2 = im_push_usecase_get(pool, pid2);
    assert(exp2 && strcmp(csilk_json_get_string(exp2, "status"), "expired") == 0);
    assert(strcmp(csilk_json_get_string(exp2, "last_error"), "测试请求已取消") == 0);
    csilk_json_free(exp2);

    unsetenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS");
    printf("  [OK] test_scheduler_ilink_channel_cancellable\n");
}

static long
mono_elapsed_ms(struct timespec t0, struct timespec t1)
{
    return (t1.tv_sec - t0.tv_sec) * 1000L + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
}

static void
test_health_handler(csilk_ctx_t* c)
{
    csilk_string(c, CSILK_STATUS_OK, "ok");
}

static int
pick_unused_loopback_port(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    assert(s >= 0);
    int opt = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = 0;
    assert(bind(s, (struct sockaddr*)&sin, sizeof(sin)) == 0);

    socklen_t len = sizeof(sin);
    assert(getsockname(s, (struct sockaddr*)&sin, &len) == 0);
    int port = ntohs(sin.sin_port);
    close(s);
    assert(port > 0);
    return port;
}

static int
connect_loopback(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return -1;
    }
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons((uint16_t)port);
    if (connect(s, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        close(s);
        return -1;
    }
    return s;
}

static int
send_all_http(int sock, const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static const char*
strcasestr_span(const char* hay, size_t hay_len, const char* needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0 || hay_len < nlen) {
        return NULL;
    }
    for (size_t i = 0; i <= hay_len - nlen; ++i) {
        if (strncasecmp(hay + i, needle, nlen) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

static bool
is_http_response_complete(const char* buf, size_t len)
{
    const char* hdr_end = NULL;
    for (size_t i = 0; i + 4 <= len; ++i) {
        if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
            hdr_end = buf + i;
            break;
        }
    }
    if (!hdr_end) {
        return false;
    }
    size_t hdr_len = (size_t)(hdr_end - buf);
    size_t body_start = hdr_len + 4;

    const char* te = strcasestr_span(buf, hdr_len, "Transfer-Encoding:");
    if (te && strcasestr_span(te, hdr_len - (size_t)(te - buf), "chunked")) {
        size_t pos = body_start;
        while (pos < len) {
            const char* crlf = NULL;
            for (size_t i = pos; i + 2 <= len; ++i) {
                if (buf[i] == '\r' && buf[i + 1] == '\n') {
                    crlf = buf + i;
                    break;
                }
            }
            if (!crlf) {
                return false;
            }
            char*         endp = NULL;
            unsigned long chunk_sz = strtoul(buf + pos, &endp, 16);
            if (endp == buf + pos) {
                return false;
            }
            pos = (size_t)(crlf - buf) + 2;
            if (chunk_sz == 0) {
                if (pos + 2 <= len && buf[pos] == '\r' && buf[pos + 1] == '\n') {
                    return true;
                }
                for (size_t i = pos; i + 4 <= len; ++i) {
                    if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
                        return true;
                    }
                }
                return false;
            }
            if (len - pos < chunk_sz + 2) {
                return false;
            }
            pos += (size_t)chunk_sz + 2;
        }
        return false;
    }

    const char* cl = strcasestr_span(buf, hdr_len, "Content-Length:");
    if (cl) {
        const char* p = cl + strlen("Content-Length:");
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        unsigned long clen = strtoul(p, NULL, 10);
        return len >= body_start + (size_t)clen;
    }

    return false;
}

static ssize_t
read_http_response(int sock, char* buf, size_t size, int timeout_ms)
{
    size_t          total = 0;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (total < size - 1) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long rem = (long)timeout_ms - mono_elapsed_ms(start, now);
        if (rem <= 0) {
            break;
        }
        struct pollfd pfd = {.fd = sock, .events = POLLIN, .revents = 0};
        int           pr = poll(&pfd, 1, (int)rem);
        if (pr <= 0) {
            if (pr < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        ssize_t n = recv(sock, buf + total, size - 1 - total, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
        buf[total] = '\0';
        if (is_http_response_complete(buf, total)) {
            break;
        }
    }
    buf[total] = '\0';
    return (ssize_t)total;
}

typedef struct {
    csilk_app_t*    app;
    csilk_server_t* srv;
    int             port;
    pthread_t       thread;
    atomic_int      exited;
    atomic_int      run_rc;
} http_test_server_t;

static void*
http_test_server_runner(void* arg)
{
    http_test_server_t* h = (http_test_server_t*)arg;
    int                 rc = csilk_app_run(h->app, h->port);
    atomic_store_explicit(&h->run_rc, rc, memory_order_release);
    atomic_store_explicit(&h->exited, 1, memory_order_release);
    return NULL;
}

static void
start_test_http_app(http_test_server_t* h)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        memset(h, 0, sizeof(*h));
        atomic_init(&h->exited, 0);
        atomic_init(&h->run_rc, 0);
        h->port = pick_unused_loopback_port();
        h->app = csilk_app_new(NULL);
        assert(h->app != NULL);

        csilk_config_t* cfg = csilk_app_config(h->app);
        assert(cfg != NULL);
        csilk_server_config_t scfg = cfg->server;
        scfg.worker_threads = 2;
        scfg.idle_timeout_ms = 10000;
        csilk_app_set_server_config(h->app, scfg);
        free(cfg);

        csilk_app_get(h->app, "/healthz", test_health_handler);
        csilk_app_use_group(h->app, "/api", jwt_middleware_wrapper);
        register_scheduler_routes(h->app);
        h->srv = csilk_app_server(h->app);
        assert(h->srv != NULL);

        assert(pthread_create(&h->thread, NULL, http_test_server_runner, h) == 0);

        bool ready = false;
        for (int wait_i = 0; wait_i < 200; ++wait_i) {
            if (atomic_load_explicit(&h->exited, memory_order_acquire) != 0) {
                break;
            }
            int fd = connect_loopback(h->port);
            if (fd >= 0) {
                const char* req =
                    "GET /healthz HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
                char resp[1024] = {0};
                if (send_all_http(fd, req, strlen(req)) == 0 &&
                    read_http_response(fd, resp, sizeof(resp), 500) > 0 &&
                    strstr(resp, "HTTP/1.1 200") != NULL) {
                    close(fd);
                    ready = true;
                    break;
                }
                close(fd);
            }
            struct timespec ts = {0, 15000000L}; /* 15ms */
            nanosleep(&ts, NULL);
        }

        if (ready) {
            return;
        }
        pthread_join(h->thread, NULL);
        csilk_app_free(h->app);
        h->app = NULL;
        h->srv = NULL;
    }
    assert(0 && "failed to start multi-worker test HTTP server");
}

static void
stop_test_http_app(http_test_server_t* h)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    csilk_server_stop(h->srv);
    pthread_join(h->thread, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long elapsed_ms = mono_elapsed_ms(t0, t1);
    assert(elapsed_ms < 2000);
    csilk_app_free(h->app);
    h->app = NULL;
    h->srv = NULL;
}

static void
test_scheduler_controller_channel_test_paths(csilk_db_pool_t* pool)
{
    signal(SIGPIPE, SIG_IGN);
    api_scheduler_channel_test(NULL);

    /* 1. Un-run warmup regression guard: freeing un-run app before live servers must NOT
     * cause subsequent csilk_app_new() address reuse to skip CSILK_HOOK_SERVER_STOP. */
    csilk_app_t* warmup_app = csilk_app_new(NULL);
    assert(warmup_app != NULL);
    register_scheduler_routes(warmup_app);
    csilk_app_free(warmup_app);

    int64_t       u = seed_user_and_active_instance(pool, "http_ctrl_u1", "bot_http1", "wx_http1");
    csilk_json_t* inst = mf_im_bot_instance_get_by_user(pool, u, "wechat", "ilink");
    assert(inst != NULL);
    int64_t inst_id = db_get_int(inst, "id");
    csilk_json_free(inst);

    channel_save_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "wechat_ilink");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "MultiWorker HTTP Test iLink");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    char uid_s[32];
    snprintf(uid_s, sizeof(uid_s), "%lld", (long long)u);
    csilk_json_t* urows = csilk_db_query_param_json(
        pool, "SELECT token_version FROM users WHERE id=?", (const char*[]){uid_s, NULL});
    assert(urows && csilk_json_array_size(urows) == 1);
    int tv = (int)db_get_int(csilk_json_array_get(urows, 0), "token_version");
    csilk_json_free(urows);

    char* jwt = jwt_generate_token(NULL, u, tv);
    assert(jwt != NULL);

    char post_req[2048];
    int  post_len = snprintf(post_req,
                             sizeof(post_req),
                             "POST /api/scheduler/channel/test HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "Authorization: Bearer %s\r\n"
                             "Content-Type: application/json\r\n"
                             "Content-Length: 2\r\n"
                             "Connection: keep-alive\r\n\r\n{}",
                             jwt);
    assert(post_len > 0 && (size_t)post_len < sizeof(post_req));

    char get_req[2048];
    int  get_len = snprintf(get_req,
                            sizeof(get_req),
                            "GET /api/scheduler/channel HTTP/1.1\r\n"
                            "Host: localhost\r\n"
                            "Authorization: Bearer %s\r\n"
                            "Connection: close\r\n\r\n",
                            jwt);
    assert(get_len > 0 && (size_t)get_len < sizeof(get_req));

    /* 2. Start live multi-worker (worker_threads = 2) server #1 */
    http_test_server_t srv1;
    start_test_http_app(&srv1);

    /* 2a. Async completion + HTTP/1.1 keep-alive connection reuse */
    setenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS", "5000", 1);
    int ka_fd = connect_loopback(srv1.port);
    assert(ka_fd >= 0);

    test_worker_ctx_t ka_ack = {.pool = pool, .inst_id = inst_id, .ack_result = "sent"};
    pthread_t         ka_ack_tid;
    assert(pthread_create(&ka_ack_tid, NULL, mock_ilink_test_ack_worker, &ka_ack) == 0);
    assert(send_all_http(ka_fd, post_req, (size_t)post_len) == 0);

    char    resp1[4096] = {0};
    ssize_t r1 = read_http_response(ka_fd, resp1, sizeof(resp1), 5000);
    pthread_join(ka_ack_tid, NULL);
    assert(r1 > 0);
    assert(strstr(resp1, "HTTP/1.1 200") != NULL);
    assert(strstr(resp1, "\"code\":0") != NULL);
    assert(strstr(resp1, "测试消息已成功发送至您的微信！") != NULL);

    /* Reuse SAME keep-alive socket for GET /api/scheduler/channel */
    assert(send_all_http(ka_fd, get_req, (size_t)get_len) == 0);
    char    resp2[4096] = {0};
    ssize_t r2 = read_http_response(ka_fd, resp2, sizeof(resp2), 3000);
    close(ka_fd);
    assert(r2 > 0);
    assert(strstr(resp2, "HTTP/1.1 200") != NULL);
    assert(strstr(resp2, "\"code\":0") != NULL);
    assert(strstr(resp2, "wechat_ilink") != NULL);

    /* 2b. 429 saturation + mid-flight multi-worker csilk_server_stop */
    setenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS", "15000", 1);

    csilk_json_t* max_q = csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(MAX(id), 0) AS mid FROM im_push_outbox WHERE mf_user_id=? AND kind='test'",
        (const char*[]){uid_s, NULL});
    assert(max_q && csilk_json_array_size(max_q) == 1);
    int64_t max_id_before = db_get_int(csilk_json_array_get(max_q, 0), "mid");
    csilk_json_free(max_q);

    char max_id_s[32];
    snprintf(max_id_s, sizeof(max_id_s), "%lld", (long long)max_id_before);

    enum { STRESS_CONNS = 22 };
    int    fds[STRESS_CONNS];
    char   rx_bufs[STRESS_CONNS][2048];
    size_t rx_lens[STRESS_CONNS];
    bool   rx_done[STRESS_CONNS];
    bool   is_429[STRESS_CONNS];

    for (int i = 0; i < STRESS_CONNS; ++i) {
        fds[i] = connect_loopback(srv1.port);
        assert(fds[i] >= 0);
        rx_bufs[i][0] = '\0';
        rx_lens[i] = 0;
        rx_done[i] = false;
        is_429[i] = false;
    }
    for (int i = 0; i < STRESS_CONNS; ++i) {
        assert(send_all_http(fds[i], post_req, (size_t)post_len) == 0);
    }

    int             overflow_429 = 0;
    int             queued_rows = 0;
    struct timespec poll_t0;
    clock_gettime(CLOCK_MONOTONIC, &poll_t0);
    while (overflow_429 < (STRESS_CONNS - 16) || queued_rows < 1) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        assert(mono_elapsed_ms(poll_t0, now) < 5000);

        struct pollfd pfds[STRESS_CONNS];
        int           pidx[STRESS_CONNS];
        int           nfds = 0;
        for (int i = 0; i < STRESS_CONNS; ++i) {
            if (!rx_done[i]) {
                pfds[nfds].fd = fds[i];
                pfds[nfds].events = POLLIN;
                pfds[nfds].revents = 0;
                pidx[nfds] = i;
                nfds++;
            }
        }
        if (nfds > 0 && poll(pfds, (nfds_t)nfds, 25) > 0) {
            for (int k = 0; k < nfds; ++k) {
                if ((pfds[k].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                    continue;
                }
                int     idx = pidx[k];
                ssize_t n = recv(fds[idx],
                                 rx_bufs[idx] + rx_lens[idx],
                                 sizeof(rx_bufs[idx]) - 1 - rx_lens[idx],
                                 0);
                if (n <= 0) {
                    if (n < 0 && errno == EINTR) {
                        continue;
                    }
                    rx_done[idx] = true;
                    continue;
                }
                rx_lens[idx] += (size_t)n;
                rx_bufs[idx][rx_lens[idx]] = '\0';
                if (is_http_response_complete(rx_bufs[idx], rx_lens[idx])) {
                    rx_done[idx] = true;
                    if (strstr(rx_bufs[idx], "\"code\":429") != NULL) {
                        assert(strstr(rx_bufs[idx], "HTTP/1.1 200") != NULL);
                        assert(strstr(rx_bufs[idx], "当前通道测试请求过多，请稍后再试") != NULL);
                        is_429[idx] = true;
                        overflow_429++;
                    }
                }
            }
        }

        csilk_json_t* qcnt =
            csilk_db_query_param_json(pool,
                                      "SELECT COUNT(*) AS cnt FROM im_push_outbox "
                                      "WHERE mf_user_id=? AND kind='test' AND id>?",
                                      (const char*[]){uid_s, max_id_s, NULL});
        if (qcnt && csilk_json_array_size(qcnt) == 1) {
            queued_rows = (int)db_get_int(csilk_json_array_get(qcnt, 0), "cnt");
        }
        if (qcnt) {
            csilk_json_free(qcnt);
        }
    }
    assert(overflow_429 >= (STRESS_CONNS - 16));
    assert(queued_rows >= 1);

    /* Stop multi-worker server mid-flight while 16 async requests remain in flight */
    stop_test_http_app(&srv1);

    /* Verify every client socket closes or finishes cleanly without hang/false-200 */
    for (int i = 0; i < STRESS_CONNS; ++i) {
        if (!rx_done[i]) {
            struct pollfd pfd = {.fd = fds[i], .events = POLLIN, .revents = 0};
            int           pr = poll(&pfd, 1, 500);
            assert(pr > 0);
            while (rx_lens[i] < sizeof(rx_bufs[i]) - 1) {
                ssize_t n =
                    recv(fds[i], rx_bufs[i] + rx_lens[i], sizeof(rx_bufs[i]) - 1 - rx_lens[i], 0);
                if (n <= 0) {
                    break;
                }
                rx_lens[i] += (size_t)n;
                rx_bufs[i][rx_lens[i]] = '\0';
            }
            assert(strstr(rx_bufs[i], "测试消息已成功发送至您的微信！") == NULL);
        }
        close(fds[i]);
    }

    /* Verify every stress test push row in outbox was cancelled & expired cleanly */
    csilk_json_t* stress_rows =
        csilk_db_query_param_json(pool,
                                  "SELECT id, status, last_error FROM im_push_outbox "
                                  "WHERE mf_user_id=? AND kind='test' AND id>? ORDER BY id ASC",
                                  (const char*[]){uid_s, max_id_s, NULL});
    assert(stress_rows != NULL);
    size_t stress_n = csilk_json_array_size(stress_rows);
    assert(stress_n >= 1);
    for (size_t i = 0; i < stress_n; ++i) {
        csilk_json_t* r = csilk_json_array_get(stress_rows, i);
        assert(strcmp(csilk_json_get_string(r, "status"), "expired") == 0);
        assert(strcmp(csilk_json_get_string(r, "last_error"), "测试请求已取消") == 0);
    }
    csilk_json_free(stress_rows);

    /* 3. Repeated start/stop cycle: 2nd server verifies shutting_down reset & pool re-init */
    http_test_server_t srv2;
    start_test_http_app(&srv2);

    setenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS", "5000", 1);
    int fd2 = connect_loopback(srv2.port);
    assert(fd2 >= 0);

    test_worker_ctx_t ack2 = {.pool = pool, .inst_id = inst_id, .ack_result = "sent"};
    pthread_t         ack2_tid;
    assert(pthread_create(&ack2_tid, NULL, mock_ilink_test_ack_worker, &ack2) == 0);
    assert(send_all_http(fd2, post_req, (size_t)post_len) == 0);

    char    resp3[4096] = {0};
    ssize_t r3 = read_http_response(fd2, resp3, sizeof(resp3), 5000);
    pthread_join(ack2_tid, NULL);
    close(fd2);
    assert(r3 > 0);
    assert(strstr(resp3, "HTTP/1.1 200") != NULL);
    assert(strstr(resp3, "\"code\":0") != NULL);
    assert(strstr(resp3, "测试消息已成功发送至您的微信！") != NULL);

    stop_test_http_app(&srv2);

    free(jwt);
    unsetenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS");
    printf("  [OK] test_scheduler_controller_channel_test_paths\n");
}

int
main(void)
{
    printf("=== Starting im_push_outbox Unit Tests ===\n");
    csilk_db_pool_t* pool = setup_db();
    test_outbox_repo_lifecycle(pool);
    test_ack_state_machine_and_expiry(pool);
    test_mark_expired_by_id(pool);
    test_inbound_validation_and_activate_switch(pool);
    test_list_active_channels(pool);
    test_rollup_variants(pool);
    test_push_usecases(pool);
    test_scheduler_ilink_channel(pool);
    test_scheduler_ilink_channel_cancellable(pool);
    test_scheduler_controller_channel_test_paths(pool);
    csilk_db_pool_free(pool);
    cleanup_db_files();
    printf("=== im_push_outbox Unit Tests Passed! ===\n");
    return 0;
}
