#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/db.h"
#include "core/scheduler/scheduler_engine.h"
#include "core/scheduler/scheduler_events.h"
#include "csilk/csilk.h"
#include "csilk/drivers/db.h"
#include "csilk/drivers/db/migration.h"
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
    assert(scheduler_engine_start(pool) == 0);
    assert(scheduler_engine_is_running() == true);

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

    /* Allow worker thread to execute */
    usleep(250000);

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

    /* 3. Test Cooldown Logic */
    assert(scheduler_event_check_and_update_cooldown("TSLA_COOLDOWN_TEST", 10) == true);
    assert(scheduler_event_check_and_update_cooldown("TSLA_COOLDOWN_TEST", 10) == false);
    assert(scheduler_event_check_and_update_cooldown("AAPL_COOLDOWN_TEST", 10) == true);

    /* 4. Test Market Event Triggering */
    int64_t mkt_job_id = mf_scheduler_repo_job_create(
        pool,
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

    /* Allow event handling and worker thread execution */
    usleep(250000);

    total_logs = 0;
    csilk_json_t* mkt_logs =
        mf_scheduler_repo_log_list(pool, 1, false, mkt_job_id, 1, 10, &total_logs);
    assert(mkt_logs != NULL);
    assert(total_logs >= 1);
    csilk_json_t* ml0 = csilk_json_array_get(mkt_logs, 0);
    assert(strcmp(csilk_json_get_string(ml0, "status"), "success") == 0);
    assert(strstr(csilk_json_get_string(ml0, "trigger_reason"), "price_spike") != NULL);
    csilk_json_free(mkt_logs);

    /* 5. Test Engine Stop */
    scheduler_engine_stop();
    assert(scheduler_engine_is_running() == false);

    csilk_db_pool_free(pool);
    printf("=== Scheduler Engine Unit Tests Passed! ===\n");
    return 0;
}
