#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "common/db.h"
#include "core/scheduler/scheduler_engine.h"
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
    printf("=== Starting Scheduler Idle Sleep & Spindown Unit Tests ===\n");
    csilk_db_init();

    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", ":memory:");
    assert(pool != NULL);

    csilk_migration_engine_t* engine = NULL;
    assert(csilk_migration_engine_new(pool, get_migrations_path(), &engine) == 0 && engine != NULL);
    int applied = 0;
    assert(csilk_migration_apply(engine, &applied) == 0);
    csilk_migration_engine_free(engine);

    /* Start scheduler engine (this seeds builtin tasks) */
    assert(scheduler_engine_start(pool) == 0);
    assert(scheduler_engine_is_running() == true);

    /* Allow the scheduler thread to perform its first tick */
    usleep(50000);

    /* Built-in jobs exist (e.g. every 5 min cron). Verify sleep_sec is clamped between 1 and 60 */
    int initial_sleep = scheduler_engine_get_last_sleep_sec();
    printf("Initial sleep calculated with builtin jobs: %d seconds\n", initial_sleep);
    assert(initial_sleep >= 1 && initial_sleep <= 60);

    /* 1. Disable all jobs to test true 0-job idle sleep */
    csilk_db_exec(pool, "UPDATE scheduled_jobs SET enabled = 0;");
    scheduler_engine_notify_reload();
    usleep(50000);

    int sleep_sec = scheduler_engine_get_last_sleep_sec();
    printf("Idle sleep calculated with 0 active jobs: %d seconds\n", sleep_sec);
    assert(sleep_sec == 60);

    /* 2. Create a future job (scheduled ~12 hours from now) and verify notify_reload wakeup latency */
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int future_hour = (tm_now.tm_hour + 12) % 24;
    char trigger_cfg[64];
    snprintf(trigger_cfg, sizeof(trigger_cfg), "{\"cron\":\"0 %d * * *\"}", future_hour);

    int64_t job_id = mf_scheduler_repo_job_create(
        pool,
        1,
        "Far Future Job",
        "Runs 12 hours from now",
        "system_handler",
        "cron",
        trigger_cfg,
        "{\"handler\":\"noop\"}",
        "silent_db",
        "{}",
        "");
    assert(job_id > 0);

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    scheduler_engine_notify_reload();
    clock_gettime(CLOCK_MONOTONIC, &ts_end);

    long elapsed_ms = (ts_end.tv_sec - ts_start.tv_sec) * 1000 +
                      (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000;
    printf("Wakeup latency after notify_reload: %ld ms\n", elapsed_ms);
    assert(elapsed_ms < 50);

    /* Give thread a short moment to wake up and process reload */
    usleep(50000);

    /* Verify that with a far future job, sleep_sec is 60 (clamped maximum) */
    sleep_sec = scheduler_engine_get_last_sleep_sec();
    printf("Idle sleep with far future job: %d seconds\n", sleep_sec);
    assert(sleep_sec == 60);

    scheduler_engine_stop();
    csilk_db_pool_free(pool);

    printf("=== Scheduler Idle Sleep & Spindown Unit Tests Passed! ===\n");
    return 0;
}
