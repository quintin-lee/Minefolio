#pragma once

/**
 * @file async_task_manager.h
 * @brief Application-layer async background task manager (bounded worker pool + persistence).
 */

#include "csilk/csilk.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Worker callback invoked on a background worker thread.
 * @param[out] out_result_json Heap-allocated JSON string owned by manager after return.
 * @return 0 on success, non-zero on failure.
 */
typedef int (*mf_async_task_fn_t)(void*   pool,
                                  int64_t task_id,
                                  int64_t user_id,
                                  int64_t ledger_id,
                                  void*   arg,
                                  char**  out_result_json,
                                  char*   err_buf,
                                  size_t  err_cap);

/**
 * @brief Cleanup callback always invoked to free arg (on completion, error, or submit failure).
 */
typedef void (*mf_async_task_cleanup_fn_t)(void* arg);

int mf_async_task_manager_init(void* pool, int num_threads);

void mf_async_task_manager_shutdown(void);

/**
 * @brief Submit an async background task (with optional active dedupe_key reuse).
 * @return task_id (>0) on success, -1 on failure (cleanup(arg) is still invoked).
 */
int64_t mf_async_task_submit(void*                      pool,
                             int64_t                    user_id,
                             int64_t                    ledger_id,
                             const char*                task_type,
                             const char*                dedupe_key,
                             const char*                initial_message,
                             mf_async_task_fn_t         fn,
                             void*                      arg,
                             mf_async_task_cleanup_fn_t cleanup,
                             bool*                      out_deduplicated);

int mf_async_task_update_progress(void* pool, int64_t task_id, int progress, const char* message);

csilk_json_t* mf_async_task_get(void* pool, int64_t user_id, int64_t task_id);

csilk_json_t* mf_async_task_list(
    void* pool, int64_t user_id, const char* status, const char* task_type, int limit);

csilk_json_t* mf_async_task_list_paged(void*       pool,
                                       int64_t     user_id,
                                       const char* status,
                                       const char* task_type,
                                       int64_t     page,
                                       int64_t     page_size,
                                       int64_t*    out_total);

/**
 * @brief Write standard Minefolio async submission envelope { task_id, task_type, status, deduplicated }.
 */
void mf_async_task_respond_submitted(
    csilk_ctx_t* c, int64_t task_id, const char* task_type, bool deduplicated, csilk_json_t* extra);

#ifdef __cplusplus
}
#endif
