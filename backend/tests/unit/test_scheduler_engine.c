#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "application/ai/im_push_usecases.h"
#include "application/scheduler/dtos.h"
#include "application/scheduler/usecases.h"
#include "common/db.h"
#include "core/scheduler/scheduler_engine.h"
#include "core/scheduler/scheduler_events.h"
#include "csilk/csilk.h"
#include "csilk/drivers/db.h"
#include "csilk/drivers/db/migration.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "infrastructure/repositories/im_push_outbox_repo_impl.h"
#include "infrastructure/repositories/scheduler_repo_impl.h"

static const char*
get_migrations_path(void)
{
    if (access("sql/migrations", F_OK) == 0) {
        return "sql/migrations";
    }
    if (access("backend/sql/migrations", F_OK) == 0) {
        return "backend/sql/migrations";
    }
    return "../sql/migrations";
}

typedef struct {
    atomic_bool* stop_flag;
    int64_t      start_seq;
} event_race_arg_t;

static void*
event_race_publisher_thread(void* arg)
{
    event_race_arg_t* ctx = (event_race_arg_t*)arg;
    int64_t           seq = ctx->start_seq;
    while (!atomic_load(ctx->stop_flag)) {
        char mkt[32];
        snprintf(mkt, sizeof(mkt), "MKT_%lld", (long long)seq);
        scheduler_event_publish_price_spike(seq, "NVDA", (seq % 10 == 0) ? 8.2 : 1.0);
        scheduler_event_publish_market_closed(mkt);
        seq++;
    }
    return NULL;
}

int
main(void)
{
    printf("=== Starting Scheduler Engine Unit Tests ===\n");
    csilk_db_init();

    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", ":memory:");
    assert(pool != NULL);

    csilk_migration_engine_t* engine = NULL;
    assert(csilk_migration_engine_new(pool, get_migrations_path(), &engine) == 0 && engine != NULL);
    int applied = 0;
    assert(csilk_migration_apply(engine, &applied) == 0);
    csilk_migration_engine_free(engine);

    /* 1. Test Engine Startup */
    scheduler_application_register_push_hooks();
    assert(scheduler_engine_start(pool) == 0);
    assert(scheduler_engine_is_running() == true);
    assert(scheduler_engine_get_ai_jobs_running() == 0);

    /* 2. Test Manual Immediate Trigger */
    int64_t job_id = mf_scheduler_repo_job_create(
        pool,
        1,
        "Nightly Analysis",
        "Nightly portfolio evaluation",
        "ai_prompt",
        "cron",
        "{\"cron\":\"0 22 * * *\"}",
        "{\"prompt\":\"Evaluate cashflow status\",\"with_holdings\":true}",
        "silent_db",
        "{}",
        "");
    assert(job_id > 0);

    assert(scheduler_engine_trigger_job_now(job_id, 1, "manual_invoke") == 0);

    /* Wait for worker thread to execute */
    int waited_ms = 0;
    while (waited_ms < 2000) {
        csilk_json_t* job = mf_scheduler_repo_job_get(pool, 1, false, job_id);
        if (job) {
            const char* st = csilk_json_get_string(job, "last_run_status");
            if (st && strcmp(st, "success") == 0) {
                csilk_json_free(job);
                break;
            }
            csilk_json_free(job);
        }
        usleep(20000);
        waited_ms += 20;
    }

    /* Verify run log */
    int64_t       total_logs = 0;
    csilk_json_t* logs = mf_scheduler_repo_log_list(pool, 1, false, job_id, 1, 10, &total_logs);
    assert(logs != NULL);
    assert(total_logs >= 1);
    csilk_json_t* l0 = csilk_json_array_get(logs, 0);
    assert(strcmp(csilk_json_get_string(l0, "status"), "success") == 0);
    assert(strcmp(csilk_json_get_string(l0, "trigger_reason"), "manual_invoke") == 0);
    assert(strstr(csilk_json_get_string(l0, "output_summary"), "AI 简报") != NULL);
    csilk_json_free(logs);

    /* Verify job last run status and next run time */
    csilk_json_t* job = mf_scheduler_repo_job_get(pool, 1, false, job_id);
    assert(job != NULL);
    assert(strcmp(csilk_json_get_string(job, "last_run_status"), "success") == 0);
    assert(strlen(csilk_json_get_string(job, "next_run_at")) > 0);
    csilk_json_free(job);
    assert(scheduler_engine_get_ai_jobs_running() == 0);

    /* 3. Test Cooldown Logic */
    assert(scheduler_event_check_and_update_cooldown("TSLA_COOLDOWN_TEST", 10) == true);
    assert(scheduler_event_check_and_update_cooldown("TSLA_COOLDOWN_TEST", 10) == false);
    assert(scheduler_event_check_and_update_cooldown("AAPL_COOLDOWN_TEST", 10) == true);

    /* 4. Test Market Event Triggering */
    int64_t mkt_job_id =
        mf_scheduler_repo_job_create(pool,
                                     1,
                                     "Spike Alert",
                                     "Detect abnormal spike",
                                     "ai_prompt",
                                     "market_event",
                                     "{\"event\":\"price_spike\",\"threshold\":5.0}",
                                     "{\"prompt\":\"Analyze market spike\"}",
                                     "silent_db",
                                     "{}",
                                     "");
    assert(mkt_job_id > 0);

    /* Trigger price spike event for NVDA */
    scheduler_event_publish_price_spike(1001, "NVDA", 8.2);

    /* Wait for event handling and worker thread execution */
    waited_ms = 0;
    while (waited_ms < 2000) {
        total_logs = 0;
        csilk_json_t* check_logs =
            mf_scheduler_repo_log_list(pool, 1, false, mkt_job_id, 1, 10, &total_logs);
        if (check_logs) {
            if (total_logs >= 1) {
                csilk_json_free(check_logs);
                break;
            }
            csilk_json_free(check_logs);
        }
        usleep(20000);
        waited_ms += 20;
    }

    total_logs = 0;
    csilk_json_t* mkt_logs =
        mf_scheduler_repo_log_list(pool, 1, false, mkt_job_id, 1, 10, &total_logs);
    assert(mkt_logs != NULL);
    assert(total_logs >= 1);
    csilk_json_t* ml0 = csilk_json_array_get(mkt_logs, 0);
    assert(strcmp(csilk_json_get_string(ml0, "status"), "success") == 0);
    assert(strstr(csilk_json_get_string(ml0, "trigger_reason"), "price_spike") != NULL);
    csilk_json_free(mkt_logs);

    /* 4b. Test Built-in Broadcast with wechat_ilink Outbox Rollup */
    const char* u_names[2] = {"bc_u1", "bc_u2"};
    int64_t     u_ids[2] = {0, 0};
    int64_t     inst_ids[2] = {0, 0};
    for (int idx = 0; idx < 2; ++idx) {
        assert(csilk_db_exec_param(pool,
                                   "INSERT INTO users (username, password) VALUES (?, 'hash')",
                                   (const char*[]){u_names[idx], NULL}) == 0);
        csilk_json_t* ures =
            csilk_db_query_param_json(pool,
                                      "SELECT id FROM users WHERE username=? LIMIT 1",
                                      (const char*[]){u_names[idx], NULL});
        assert(ures && csilk_json_array_size(ures) == 1);
        u_ids[idx] = db_get_int(csilk_json_array_get(ures, 0), "id");
        csilk_json_free(ures);

        inst_ids[idx] = mf_im_bot_instance_create(pool, u_ids[idx], "wechat", "ilink");
        assert(inst_ids[idx] > 0);
        char ierr[128] = {0};
        char bot[32], wx[32];
        snprintf(bot, sizeof(bot), "bot_bc_%d", idx);
        snprintf(wx, sizeof(wx), "wx_bc_%d", idx);
        assert(mf_im_bot_instance_activate(pool, inst_ids[idx], bot, wx, ierr, sizeof(ierr)) == 0);

        channel_save_dto_t cdto;
        memset(&cdto, 0, sizeof(cdto));
        snprintf(cdto.channel_type, sizeof(cdto.channel_type), "wechat_ilink");
        snprintf(cdto.channel_name, sizeof(cdto.channel_name), "微信 iLink");
        assert(scheduler_usecase_save_channel(pool, u_ids[idx], &cdto) == 0);
    }

    int64_t bi_job_id =
        mf_scheduler_repo_job_create_builtin(pool,
                                             "Builtin Broadcast Test",
                                             "Verify builtin broadcast to wechat_ilink outbox",
                                             "system_builtin",
                                             "cron",
                                             "{\"cron\":\"0 8 * * *\"}",
                                             "{\"handler\":\"mcp_pool_cleanup\"}",
                                             "user_default",
                                             "{}",
                                             "2099-01-01 00:00:00");
    assert(bi_job_id > 0);
    assert(scheduler_engine_trigger_job_now(bi_job_id, 0, "cron_tick") == 0);

    int64_t bi_log_id = 0;
    waited_ms = 0;
    while (waited_ms < 2000) {
        total_logs = 0;
        csilk_json_t* bi_logs =
            mf_scheduler_repo_log_list(pool, 0, true, bi_job_id, 1, 10, &total_logs);
        if (bi_logs && csilk_json_array_size(bi_logs) > 0) {
            csilk_json_t* b0 = csilk_json_array_get(bi_logs, 0);
            const char*   st = csilk_json_get_string(b0, "status");
            const char*   ns = csilk_json_get_string(b0, "notification_status");
            if (st && strcmp(st, "success") == 0 && ns && strcmp(ns, "queued") == 0) {
                bi_log_id = db_get_int(b0, "id");
                csilk_json_free(bi_logs);
                break;
            }
        }
        if (bi_logs) {
            csilk_json_free(bi_logs);
        }
        usleep(20000);
        waited_ms += 20;
    }
    assert(bi_log_id > 0);

    for (int idx = 0; idx < 2; ++idx) {
        csilk_json_t* items = NULL;
        int           code = 0;
        char          err[128] = {0};
        assert(im_push_usecase_gateway_claim(pool,
                                             inst_ids[idx],
                                             true,
                                             "2026-11-25T13:10:00Z",
                                             5,
                                             &items,
                                             &code,
                                             err,
                                             sizeof(err)) == 0);
        assert(items && csilk_json_array_size(items) == 1);
        int64_t pid = db_get_int(csilk_json_array_get(items, 0), "id");
        csilk_json_free(items);
        csilk_json_t* outbox_row = mf_im_push_outbox_get(pool, pid);
        assert(outbox_row && db_get_int(outbox_row, "job_run_log_id") == bi_log_id);
        csilk_json_free(outbox_row);
        assert(im_push_usecase_gateway_ack(
                   pool, inst_ids[idx], pid, "sent", "", 0, &code, err, sizeof(err)) == 0);
    }

    total_logs = 0;
    csilk_json_t* bi_done =
        mf_scheduler_repo_log_list(pool, 0, true, bi_job_id, 1, 10, &total_logs);
    assert(bi_done && csilk_json_array_size(bi_done) > 0);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(bi_done, 0), "notification_status"),
                  "sent") == 0);
    csilk_json_free(bi_done);

    /* Add disabled user bc_u3 with wechat_ilink -> enqueue failure persists error & rolls up partial */
    assert(csilk_db_exec_param(pool,
                               "INSERT INTO users (username, password) VALUES (?, 'hash')",
                               (const char*[]){"bc_u3", NULL}) == 0);
    csilk_json_t* u3res = csilk_db_query_param_json(
        pool, "SELECT id FROM users WHERE username=? LIMIT 1", (const char*[]){"bc_u3", NULL});
    assert(u3res && csilk_json_array_size(u3res) == 1);
    int64_t u3_id = db_get_int(csilk_json_array_get(u3res, 0), "id");
    csilk_json_free(u3res);

    int64_t inst3_id = mf_im_bot_instance_create(pool, u3_id, "wechat", "ilink");
    assert(inst3_id > 0);
    char ierr3[128] = {0};
    assert(mf_im_bot_instance_activate(
               pool, inst3_id, "bot_bc_2", "wx_bc_2", ierr3, sizeof(ierr3)) == 0);
    assert(mf_im_bot_instance_set_enabled(pool, u3_id, inst3_id, 0) == 0);

    channel_save_dto_t cdto3;
    memset(&cdto3, 0, sizeof(cdto3));
    snprintf(cdto3.channel_type, sizeof(cdto3.channel_type), "wechat_ilink");
    snprintf(cdto3.channel_name, sizeof(cdto3.channel_name), "微信 iLink 3");
    assert(scheduler_usecase_save_channel(pool, u3_id, &cdto3) == 0);

    assert(scheduler_engine_trigger_job_now(bi_job_id, 0, "cron_tick") == 0);
    int64_t bi_log2_id = 0;
    waited_ms = 0;
    while (waited_ms < 2000) {
        total_logs = 0;
        csilk_json_t* bi_logs2 =
            mf_scheduler_repo_log_list(pool, 0, true, bi_job_id, 1, 10, &total_logs);
        if (bi_logs2) {
            for (size_t i = 0; i < csilk_json_array_size(bi_logs2); ++i) {
                csilk_json_t* row = csilk_json_array_get(bi_logs2, i);
                int64_t       lid = db_get_int(row, "id");
                const char*   st = csilk_json_get_string(row, "status");
                const char*   ns = csilk_json_get_string(row, "notification_status");
                if (lid > bi_log_id && st && strcmp(st, "success") == 0 && ns &&
                    strcmp(ns, "queued") == 0) {
                    assert(db_get_int(row, "notif_sync_failed") == 1);
                    const char* em = csilk_json_get_string(row, "error_message");
                    assert(em != NULL && em[0] != '\0');
                    bi_log2_id = lid;
                    break;
                }
            }
            csilk_json_free(bi_logs2);
            if (bi_log2_id > 0) {
                break;
            }
        }
        usleep(20000);
        waited_ms += 20;
    }
    assert(bi_log2_id > 0);

    for (int idx = 0; idx < 2; ++idx) {
        csilk_json_t* items = NULL;
        int           code = 0;
        char          err[128] = {0};
        assert(im_push_usecase_gateway_claim(pool,
                                             inst_ids[idx],
                                             true,
                                             "2026-11-25T13:15:00Z",
                                             5,
                                             &items,
                                             &code,
                                             err,
                                             sizeof(err)) == 0);
        assert(items && csilk_json_array_size(items) == 1);
        int64_t pid = db_get_int(csilk_json_array_get(items, 0), "id");
        csilk_json_free(items);
        csilk_json_t* outbox_row = mf_im_push_outbox_get(pool, pid);
        assert(outbox_row && db_get_int(outbox_row, "job_run_log_id") == bi_log2_id);
        csilk_json_free(outbox_row);
        assert(im_push_usecase_gateway_ack(
                   pool, inst_ids[idx], pid, "sent", "", 0, &code, err, sizeof(err)) == 0);
    }

    total_logs = 0;
    csilk_json_t* bi_part =
        mf_scheduler_repo_log_list(pool, 0, true, bi_job_id, 1, 10, &total_logs);
    assert(bi_part != NULL);
    bool found_partial = false;
    for (size_t i = 0; i < csilk_json_array_size(bi_part); ++i) {
        csilk_json_t* row = csilk_json_array_get(bi_part, i);
        if (db_get_int(row, "id") == bi_log2_id) {
            assert(strcmp(csilk_json_get_string(row, "notification_status"), "partial") == 0);
            const char* em = csilk_json_get_string(row, "error_message");
            assert(em != NULL && em[0] != '\0');
            found_partial = true;
            break;
        }
    }
    assert(found_partial);
    csilk_json_free(bi_part);

    /* 5. Test Engine Stop */
    scheduler_engine_stop();
    assert(scheduler_engine_is_running() == false);
    assert(scheduler_engine_get_ai_jobs_running() == 0);

    /* 6. Test Concurrent Event Publish During Engine Stop (Shutdown Race) */
    for (int cycle = 0; cycle < 5; cycle++) {
        assert(scheduler_engine_start(pool) == 0);

        atomic_bool      stop_flag = false;
        event_race_arg_t arg0 = {.stop_flag = &stop_flag, .start_seq = 10000 + cycle * 100000};
        event_race_arg_t arg1 = {.stop_flag = &stop_flag, .start_seq = 5000000 + cycle * 100000};
        pthread_t        t0;
        pthread_t        t1;
        assert(pthread_create(&t0, NULL, event_race_publisher_thread, &arg0) == 0);
        assert(pthread_create(&t1, NULL, event_race_publisher_thread, &arg1) == 0);

        usleep(5000);
        scheduler_engine_stop();
        usleep(2000);

        atomic_store(&stop_flag, true);
        pthread_join(t0, NULL);
        pthread_join(t1, NULL);

        assert(scheduler_engine_is_running() == false);
        assert(scheduler_engine_get_ai_jobs_running() == 0);
    }

    csilk_db_pool_free(pool);
    printf("=== Scheduler Engine Unit Tests Passed! ===\n");
    return 0;
}
