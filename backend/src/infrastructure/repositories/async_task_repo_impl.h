#pragma once

#include "csilk/csilk.h"
#include "common/db.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mark leftover pending/running tasks as failed after process restart.
 */
int mf_async_task_repo_fail_stale(csilk_db_pool_t* pool);

/**
 * @brief Look up an active (pending/running) task matching (user_id, dedupe_key).
 * @return 1 if found, 0 if not found, -1 on error.
 */
int mf_async_task_repo_find_active_dedupe(csilk_db_pool_t* pool,
                                          int64_t          user_id,
                                          const char*      dedupe_key,
                                          int64_t*         out_task_id);

/**
 * @brief Insert a new pending async task and return its generated id (>0), or -1 on error.
 */
int64_t mf_async_task_repo_insert(csilk_db_pool_t* pool,
                                  int64_t          user_id,
                                  int64_t          ledger_id,
                                  const char*      task_type,
                                  const char*      dedupe_key,
                                  const char*      initial_message);

int mf_async_task_repo_mark_running(csilk_db_pool_t* pool, int64_t task_id, const char* message);

int mf_async_task_repo_update_progress(csilk_db_pool_t* pool,
                                       int64_t          task_id,
                                       int              progress,
                                       const char*      message);

int mf_async_task_repo_mark_completed(csilk_db_pool_t* pool,
                                      int64_t          task_id,
                                      const char*      result_json,
                                      const char*      message);

int mf_async_task_repo_mark_failed(csilk_db_pool_t* pool, int64_t task_id, const char* error_msg);

/**
 * @brief Fetch a single async task scoped to user_id, with result parsed as JSON when valid.
 */
csilk_json_t* mf_async_task_repo_get(csilk_db_pool_t* pool, int64_t user_id, int64_t task_id);

/**
 * @brief List recent async tasks for user_id with optional status and task_type filters.
 */
csilk_json_t* mf_async_task_repo_list(csilk_db_pool_t* pool,
                                      int64_t          user_id,
                                      const char*      status_filter,
                                      const char*      type_filter,
                                      int              limit);

csilk_json_t* mf_async_task_repo_list_paged(csilk_db_pool_t* pool,
                                            int64_t          user_id,
                                            const char*      status_filter,
                                            const char*      type_filter,
                                            int64_t          page,
                                            int64_t          page_size,
                                            int64_t*         out_total);

#ifdef __cplusplus
}
#endif
