#include "application/task/async_task_manager.h"
#include "infrastructure/repositories/async_task_repo_impl.h"
#include "domain/task/entity.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int  value;
    bool should_fail;
} test_work_arg_t;

static int
sample_success_task(void*   pool,
                    int64_t task_id,
                    int64_t user_id,
                    int64_t ledger_id,
                    void*   arg,
                    char**  out_result_json,
                    char*   err_buf,
                    size_t  err_cap)
{
    (void)user_id;
    (void)ledger_id;
    test_work_arg_t* w = (test_work_arg_t*)arg;
    mf_async_task_update_progress(pool, task_id, 50, "处理一半");

    if (w->should_fail) {
        snprintf(err_buf, err_cap, "模拟执行失败");
        return -1;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"computed\":%d}", w->value * 2);
    *out_result_json = strdup(buf);
    return 0;
}

/* Keeps a task active long enough to exercise dedupe of concurrent submissions. */
static int
slow_task(void*   pool,
          int64_t  task_id,
          int64_t  user_id,
          int64_t  ledger_id,
          void*    arg,
          char**   out_result_json,
          char*    err_buf,
          size_t   err_cap)
{
    (void)pool;
    (void)task_id;
    (void)user_id;
    (void)ledger_id;
    (void)arg;
    (void)err_buf;
    (void)err_cap;
    usleep(300000);
    *out_result_json = strdup("{\"ok\":true}");
    return 0;
}

static int
wait_for_terminal(csilk_db_pool_t* pool, int64_t user_id, int64_t task_id, csilk_json_t** out_obj)
{
    for (int i = 0; i < 100; i++) {
        csilk_json_t* t = mf_async_task_get(pool, user_id, task_id);
        if (t) {
            const char* st = csilk_json_get_string(t, "status");
            if (mf_task_is_terminal_status(st)) {
                *out_obj = t;
                return 0;
            }
            csilk_json_free(t);
        }
        usleep(20000);
    }
    return -1;
}

int
main(void)
{
    printf("Running test_async_task_manager...\n");
    unlink("/tmp/test_async_task_manager.db");
    setenv("MINEFOLIO_DB_DRIVER", "sqlite", 1);
    setenv("MINEFOLIO_DB_DSN", "/tmp/test_async_task_manager.db", 1);

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    /* Seed stale pending row before manager init to verify restart recovery */
    int64_t stale_id =
        mf_async_task_repo_insert(pool, 1, 0, "market_sync", "market:sync:all", "stale");
    assert(stale_id > 0);
    assert(mf_async_task_manager_init(pool, 2) == 0);

    csilk_json_t* stale = mf_async_task_get(pool, 1, stale_id);
    assert(stale != NULL);
    assert(strcmp(csilk_json_get_string(stale, "status"), "failed") == 0);
    csilk_json_free(stale);

    /* 1. Submit successful task and verify completion + user isolation */
    test_work_arg_t* arg1 = calloc(1, sizeof(*arg1));
    arg1->value = 21;
    arg1->should_fail = false;
    bool    dedup = false;
    int64_t t1 = mf_async_task_submit(
        pool, 1, 0, "unit_test", "unit:key:1", "排队中", sample_success_task, arg1, free, &dedup);
    assert(t1 > 0 && !dedup);

    /* Cross-user lookup must return NULL */
    assert(mf_async_task_get(pool, 2, t1) == NULL);

    csilk_json_t* done1 = NULL;
    assert(wait_for_terminal(pool, 1, t1, &done1) == 0 && done1 != NULL);
    assert(strcmp(csilk_json_get_string(done1, "status"), "completed") == 0);
    assert((int)csilk_json_get_number(done1, "progress") == 100);
    const csilk_json_t* res1 = csilk_json_get(done1, "result");
    assert(res1 && (int)csilk_json_get_number(res1, "computed") == 42);
    csilk_json_free(done1);

    /* 2. Submit failing task and verify error_message */
    test_work_arg_t* arg2 = calloc(1, sizeof(*arg2));
    arg2->should_fail = true;
    int64_t t2 = mf_async_task_submit(
        pool, 1, 0, "unit_test", "", "排队中", sample_success_task, arg2, free, NULL);
    assert(t2 > 0);

    csilk_json_t* done2 = NULL;
    assert(wait_for_terminal(pool, 1, t2, &done2) == 0 && done2 != NULL);
    assert(strcmp(csilk_json_get_string(done2, "status"), "failed") == 0);
    assert(strcmp(csilk_json_get_string(done2, "error_message"), "模拟执行失败") == 0);
    csilk_json_free(done2);

    /* 3. List tasks */
    csilk_json_t* list = mf_async_task_list(pool, 1, NULL, "unit_test", 10);
    assert(list != NULL && csilk_json_array_size(list) == 2);
    csilk_json_free(list);

    /* 4. Concurrent duplicate submission reuses the active deduped task */
    bool    dedup_first = false;
    int64_t d1 = mf_async_task_submit(
        pool, 1, 0, "unit_test", "unit:dedupe", "排队中", slow_task, NULL, NULL, &dedup_first);
    assert(d1 > 0 && !dedup_first);
    bool    dedup_second = false;
    int64_t d2 = mf_async_task_submit(
        pool, 1, 0, "unit_test", "unit:dedupe", "排队中", slow_task, NULL, NULL, &dedup_second);
    assert(d2 == d1 && dedup_second);
    csilk_json_t* done_d = NULL;
    assert(wait_for_terminal(pool, 1, d1, &done_d) == 0 && done_d != NULL);
    csilk_json_free(done_d);

    mf_async_task_manager_shutdown();
    csilk_db_pool_free(pool);
    unlink("/tmp/test_async_task_manager.db");
    printf("PASS: test_async_task_manager completed successfully\n");
    return 0;
}
