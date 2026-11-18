#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "csilk/csilk.h"
#include "csilk/drivers/db.h"
#include "csilk/drivers/db/migration.h"
#include "common/db.h"
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
    printf("=== Starting Scheduler Repository Unit Tests ===\n");
    csilk_db_init();

    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", ":memory:");
    assert(pool != NULL);

    csilk_migration_engine_t* engine = NULL;
    assert(csilk_migration_engine_new(pool, get_migrations_path(), &engine) == 0 && engine != NULL);
    int applied = 0;
    assert(csilk_migration_apply(engine, &applied) == 0);
    csilk_migration_engine_free(engine);

    /* 1. Test Job Create */
    int64_t job_id = mf_scheduler_repo_job_create(
        pool,
        1, /* user_id */
        "AI Daily Digest",
        "Summarize US market",
        "ai_prompt",
        "cron",
        "{\"cron\":\"0 5 * * *\"}",
        "{\"prompt\":\"Summarize my portfolio\",\"with_holdings\":true}",
        "user_default",
        "{}",
        "2026-11-19 05:00:00");
    assert(job_id > 0);

    /* 2. Test Job Get */
    csilk_json_t* job = mf_scheduler_repo_job_get(pool, 1, false, job_id);
    assert(job != NULL);
    assert(strcmp(csilk_json_get_string(job, "name"), "AI Daily Digest") == 0);
    assert(strcmp(csilk_json_get_string(job, "job_type"), "ai_prompt") == 0);
    assert(db_get_int(job, "enabled") == 1);
    csilk_json_free(job);

    /* 3. Test Multi-tenancy Isolation: User 2 cannot access user 1's job */
    csilk_json_t* job_u2 = mf_scheduler_repo_job_get(pool, 2, false, job_id);
    assert(job_u2 == NULL);

    /* 4. Test Job List */
    int64_t total = 0;
    csilk_json_t* list = mf_scheduler_repo_job_list(pool, 1, false, 1, 10, &total);
    assert(list != NULL);
    assert(total == 1);
    assert(csilk_json_array_size(list) == 1);
    csilk_json_free(list);

    /* 5. Test Job Update */
    int rc = mf_scheduler_repo_job_update(
        pool,
        1,
        false,
        job_id,
        "AI Daily Digest Updated",
        "Updated desc",
        "cron",
        "{\"cron\":\"0 6 * * *\"}",
        "{\"prompt\":\"New prompt\"}",
        "user_default",
        "{}",
        "2026-11-19 06:00:00");
    assert(rc == 0);

    job = mf_scheduler_repo_job_get(pool, 1, false, job_id);
    assert(job != NULL);
    assert(strcmp(csilk_json_get_string(job, "name"), "AI Daily Digest Updated") == 0);
    csilk_json_free(job);

    /* 6. Test Job Toggle Enabled */
    rc = mf_scheduler_repo_job_set_enabled(pool, 1, false, job_id, false);
    assert(rc == 0);
    job = mf_scheduler_repo_job_get(pool, 1, false, job_id);
    assert(job != NULL);
    assert(db_get_int(job, "enabled") == 0);
    csilk_json_free(job);

    /* 7. Test Job Update Last Run */
    rc = mf_scheduler_repo_job_update_last_run(pool, job_id, "success", 340, "2026-11-20 06:00:00");
    assert(rc == 0);
    job = mf_scheduler_repo_job_get(pool, 1, false, job_id);
    assert(job != NULL);
    assert(strcmp(csilk_json_get_string(job, "last_run_status"), "success") == 0);
    assert(db_get_int(job, "last_run_duration_ms") == 340);
    csilk_json_free(job);

    /* 8. Test User Notification Channel */
    csilk_json_t* ch = mf_scheduler_repo_get_user_channel(pool, 1);
    assert(ch == NULL); /* not created yet */

    rc = mf_scheduler_repo_save_user_channel(pool, 1, "serverchan", "My WeChat", "https://sctapi.ftqq.com", "SCT_SECRET_KEY");
    assert(rc == 0);

    ch = mf_scheduler_repo_get_user_channel(pool, 1);
    assert(ch != NULL);
    assert(strcmp(csilk_json_get_string(ch, "channel_type"), "serverchan") == 0);
    assert(strcmp(csilk_json_get_string(ch, "secret_key"), "SCT_SECRET_KEY") == 0);
    csilk_json_free(ch);

    /* Test updating user channel */
    rc = mf_scheduler_repo_save_user_channel(pool, 1, "pushdeer", "My WeChat 2", "", "PDU12345");
    assert(rc == 0);
    ch = mf_scheduler_repo_get_user_channel(pool, 1);
    assert(ch != NULL);
    assert(strcmp(csilk_json_get_string(ch, "channel_type"), "pushdeer") == 0);
    assert(strcmp(csilk_json_get_string(ch, "secret_key"), "PDU12345") == 0);
    csilk_json_free(ch);

    /* 9. Test Job Run Logs */
    int64_t log_id = mf_scheduler_repo_log_create(pool, job_id, 1, "cron_tick");
    assert(log_id > 0);

    rc = mf_scheduler_repo_log_complete(pool, log_id, "success", 280, "Brief generated OK", "", "sent");
    assert(rc == 0);

    int64_t log_total = 0;
    csilk_json_t* logs = mf_scheduler_repo_log_list(pool, 1, false, job_id, 1, 10, &log_total);
    assert(logs != NULL);
    assert(log_total == 1);
    assert(csilk_json_array_size(logs) == 1);
    csilk_json_t* l0 = csilk_json_array_get(logs, 0);
    assert(strcmp(csilk_json_get_string(l0, "status"), "success") == 0);
    assert(strcmp(csilk_json_get_string(l0, "output_summary"), "Brief generated OK") == 0);
    assert(strcmp(csilk_json_get_string(l0, "notification_status"), "sent") == 0);
    csilk_json_free(logs);

    /* 10. Test Job Delete */
    rc = mf_scheduler_repo_job_delete(pool, 1, job_id);
    assert(rc == 0);

    job = mf_scheduler_repo_job_get(pool, 1, false, job_id);
    assert(job == NULL);

    csilk_db_pool_free(pool);
    printf("=== Scheduler Repository Unit Tests Passed! ===\n");
    return 0;
}
