#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "csilk/csilk.h"
#include "csilk/drivers/db.h"
#include "csilk/drivers/db/migration.h"

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
    printf("=== Test Scheduler Migration V029 ===\n");
    csilk_db_init();

    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", ":memory:");
    assert(pool != NULL);

    csilk_migration_engine_t* engine = NULL;
    int rc = csilk_migration_engine_new(pool, get_migrations_path(), &engine);
    assert(rc == 0 && engine != NULL);

    int applied = 0;
    rc = csilk_migration_apply(engine, &applied);
    printf("csilk_migration_apply: rc=%d, applied=%d\n", rc, applied);
    assert(rc == 0);
    csilk_migration_engine_free(engine);

    /* Verify scheduled_jobs table exists */
    csilk_json_t* res = csilk_db_query_json(
        pool,
        "SELECT count(*) as cnt FROM sqlite_master WHERE type='table' AND name='scheduled_jobs';");
    assert(res != NULL);
    csilk_json_t* row = csilk_json_array_get(res, 0);
    const char* cnt_str = csilk_json_get_string(row, "cnt");
    assert(cnt_str && atoi(cnt_str) == 1);
    csilk_json_free(res);

    /* Verify user_notification_channels table exists */
    res = csilk_db_query_json(
        pool,
        "SELECT count(*) as cnt FROM sqlite_master WHERE type='table' AND name='user_notification_channels';");
    assert(res != NULL);
    row = csilk_json_array_get(res, 0);
    cnt_str = csilk_json_get_string(row, "cnt");
    assert(cnt_str && atoi(cnt_str) == 1);
    csilk_json_free(res);

    /* Verify job_run_logs table exists */
    res = csilk_db_query_json(
        pool,
        "SELECT count(*) as cnt FROM sqlite_master WHERE type='table' AND name='job_run_logs';");
    assert(res != NULL);
    row = csilk_json_array_get(res, 0);
    cnt_str = csilk_json_get_string(row, "cnt");
    assert(cnt_str && atoi(cnt_str) == 1);
    csilk_json_free(res);

    /* Verify inserting test job with is_builtin (V030) */
    rc = csilk_db_exec_param(
        pool,
        "INSERT INTO scheduled_jobs (user_id, name, description, job_type, trigger_type, trigger_config, payload_config, delivery_channel, is_builtin) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
        (const char*[]){"1", "Test Job", "Desc", "ai_prompt", "cron", "{\"cron\":\"0 5 * * *\"}", "{\"prompt\":\"test\"}", "wechat", "1", NULL});
    assert(rc == 0);

    /* Verify inserting test channel */
    rc = csilk_db_exec_param(
        pool,
        "INSERT INTO user_notification_channels (user_id, channel_type, webhook_url, secret_key) "
        "VALUES (?, ?, ?, ?);",
        (const char*[]){"1", "serverchan", "https://sctapi.ftqq.com", "SCT123", NULL});
    assert(rc == 0);

    /* Verify inserting test log */
    rc = csilk_db_exec_param(
        pool,
        "INSERT INTO job_run_logs (job_id, user_id, status, duration_ms, trigger_reason, output_summary) "
        "VALUES (?, ?, ?, ?, ?, ?);",
        (const char*[]){"1", "1", "success", "120", "cron_tick", "Test brief", NULL});
    assert(rc == 0);

    csilk_db_pool_free(pool);
    printf("=== Test Scheduler Migration V029 Passed! ===\n");
    return 0;
}
