#include "application/task/async_task_manager.h"
#include "infrastructure/repositories/async_task_repo_impl.h"
#include "core/scheduler/scheduler_worker_pool.h"
#include "common/db.h"
#include "common/response.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    csilk_db_pool_t*           pool;
    int64_t                    task_id;
    int64_t                    user_id;
    int64_t                    ledger_id;
    mf_async_task_fn_t         fn;
    void*                      arg;
    mf_async_task_cleanup_fn_t cleanup;
} async_task_work_item_t;

static scheduler_worker_pool_t* s_async_pool = NULL;
static pthread_mutex_t          s_mgr_mutex = PTHREAD_MUTEX_INITIALIZER;

static void
execute_task_worker(void* raw)
{
    async_task_work_item_t* item = (async_task_work_item_t*)raw;
    if (!item) {
        return;
    }

    mf_async_task_repo_mark_running(item->pool, item->task_id, "处理中");

    char* result_json = NULL;
    char  err_buf[512] = {0};
    int   rc = -1;
    if (item->fn) {
        rc = item->fn(item->pool,
                      item->task_id,
                      item->user_id,
                      item->ledger_id,
                      item->arg,
                      &result_json,
                      err_buf,
                      sizeof(err_buf));
    }

    if (rc == 0) {
        mf_async_task_repo_mark_completed(
            item->pool, item->task_id, result_json ? result_json : "{}", "已完成");
    } else {
        mf_async_task_repo_mark_failed(
            item->pool, item->task_id, err_buf[0] ? err_buf : "后台任务执行失败");
    }

    free(result_json);
    if (item->cleanup) {
        item->cleanup(item->arg);
    }
    free(item);
}

int
mf_async_task_manager_init(void* pool, int num_threads)
{
    pthread_mutex_lock(&s_mgr_mutex);
    if (!s_async_pool) {
        int threads = num_threads > 0 ? num_threads : 4;
        s_async_pool = scheduler_worker_pool_create(threads);
    }
    pthread_mutex_unlock(&s_mgr_mutex);

    if (!s_async_pool) {
        return -1;
    }
    if (pool) {
        mf_async_task_repo_fail_stale((csilk_db_pool_t*)pool);
    }
    return 0;
}

void
mf_async_task_manager_shutdown(void)
{
    pthread_mutex_lock(&s_mgr_mutex);
    scheduler_worker_pool_t* p = s_async_pool;
    s_async_pool = NULL;
    pthread_mutex_unlock(&s_mgr_mutex);

    if (p) {
        scheduler_worker_pool_destroy(p);
    }
}

int64_t
mf_async_task_submit(void*                      pool,
                     int64_t                    user_id,
                     int64_t                    ledger_id,
                     const char*                task_type,
                     const char*                dedupe_key,
                     const char*                initial_message,
                     mf_async_task_fn_t         fn,
                     void*                      arg,
                     mf_async_task_cleanup_fn_t cleanup,
                     bool*                      out_deduplicated)
{
    if (out_deduplicated) {
        *out_deduplicated = false;
    }
    if (!pool || user_id <= 0 || !task_type || !task_type[0] || !fn) {
        if (cleanup) {
            cleanup(arg);
        }
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    pthread_mutex_lock(&s_mgr_mutex);
    if (!s_async_pool) {
        s_async_pool = scheduler_worker_pool_create(4);
    }
    scheduler_worker_pool_t* worker_pool = s_async_pool;

    if (!worker_pool) {
        pthread_mutex_unlock(&s_mgr_mutex);
        if (cleanup) {
            cleanup(arg);
        }
        return -1;
    }

    if (dedupe_key && dedupe_key[0]) {
        int64_t existing_id = 0;
        if (mf_async_task_repo_find_active_dedupe(db_pool, user_id, dedupe_key, &existing_id) > 0 &&
            existing_id > 0) {
            pthread_mutex_unlock(&s_mgr_mutex);
            if (cleanup) {
                cleanup(arg);
            }
            if (out_deduplicated) {
                *out_deduplicated = true;
            }
            return existing_id;
        }
    }

    int64_t task_id = mf_async_task_repo_insert(
        db_pool, user_id, ledger_id, task_type, dedupe_key, initial_message);
    if (task_id <= 0) {
        pthread_mutex_unlock(&s_mgr_mutex);
        if (cleanup) {
            cleanup(arg);
        }
        return -1;
    }

    async_task_work_item_t* item = calloc(1, sizeof(*item));
    if (!item) {
        pthread_mutex_unlock(&s_mgr_mutex);
        mf_async_task_repo_mark_failed(db_pool, task_id, "内存不足");
        if (cleanup) {
            cleanup(arg);
        }
        return -1;
    }
    item->pool = db_pool;
    item->task_id = task_id;
    item->user_id = user_id;
    item->ledger_id = ledger_id;
    item->fn = fn;
    item->arg = arg;
    item->cleanup = cleanup;

    if (scheduler_worker_pool_submit(worker_pool, execute_task_worker, item) != 0) {
        pthread_mutex_unlock(&s_mgr_mutex);
        free(item);
        mf_async_task_repo_mark_failed(db_pool, task_id, "后台任务队列已关闭");
        if (cleanup) {
            cleanup(arg);
        }
        return -1;
    }
    pthread_mutex_unlock(&s_mgr_mutex);

    return task_id;
}

int
mf_async_task_update_progress(void* pool, int64_t task_id, int progress, const char* message)
{
    return mf_async_task_repo_update_progress((csilk_db_pool_t*)pool, task_id, progress, message);
}

csilk_json_t*
mf_async_task_get(void* pool, int64_t user_id, int64_t task_id)
{
    return mf_async_task_repo_get((csilk_db_pool_t*)pool, user_id, task_id);
}

csilk_json_t*
mf_async_task_list(
    void* pool, int64_t user_id, const char* status, const char* task_type, int limit)
{
    return mf_async_task_repo_list((csilk_db_pool_t*)pool, user_id, status, task_type, limit);
}

void
mf_async_task_respond_submitted(
    csilk_ctx_t* c, int64_t task_id, const char* task_type, bool deduplicated, csilk_json_t* extra)
{
    csilk_json_t* data = extra ? extra : csilk_json_object();
    csilk_json_add_number(data, "task_id", (double)task_id);
    csilk_json_add_string(data, "task_type", task_type ? task_type : "");
    csilk_json_add_string(data, "status", "pending");
    csilk_json_add_bool(data, "deduplicated", deduplicated);
    respond_ok(c, data);
}
