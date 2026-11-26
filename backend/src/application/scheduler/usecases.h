#pragma once

#include "application/scheduler/dtos.h"
#include "core/scheduler/scheduler_engine.h" /* scheduler_push_hooks_t */
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int
scheduler_usecase_dashboard(void* pool, int64_t user_id, bool is_admin, csilk_json_t** out_resp);

int scheduler_usecase_job_list(void*          pool,
                               int64_t        user_id,
                               bool           is_admin,
                               int64_t        page,
                               int64_t        page_size,
                               csilk_json_t** out_resp);

int scheduler_usecase_job_get(
    void* pool, int64_t user_id, bool is_admin, int64_t job_id, csilk_json_t** out_job);

int scheduler_usecase_job_create(void*                   pool,
                                 int64_t                 user_id,
                                 const job_create_dto_t* dto,
                                 csilk_json_t**          out_job);

int scheduler_usecase_job_update(
    void* pool, int64_t user_id, bool is_admin, int64_t job_id, const job_update_dto_t* dto);

int scheduler_usecase_job_delete(void* pool, int64_t user_id, int64_t job_id);

int scheduler_usecase_job_toggle(
    void* pool, int64_t user_id, bool is_admin, int64_t job_id, bool enabled);

int scheduler_usecase_job_run_now(void* pool, int64_t user_id, bool is_admin, int64_t job_id);

int scheduler_usecase_job_logs(void*          pool,
                               int64_t        user_id,
                               bool           is_admin,
                               int64_t        job_id,
                               int64_t        page,
                               int64_t        page_size,
                               csilk_json_t** out_resp);

int scheduler_usecase_get_channel(void* pool, int64_t user_id, csilk_json_t** out_resp);

int scheduler_usecase_save_channel(void* pool, int64_t user_id, const channel_save_dto_t* dto);

int scheduler_usecase_seed_builtin_jobs(void* pool);

int scheduler_usecase_test_channel(void* pool, int64_t user_id, char* err_buf, size_t err_len);

int scheduler_usecase_test_channel_cancellable(void*   pool,
                                               int64_t user_id,
                                               int (*is_alive_fn)(void*),
                                               void*  alive_ctx,
                                               char*  err_buf,
                                               size_t err_len);

/**
 * Register the application-layer iLink push hooks into the scheduler engine.
 * Call once at startup, before scheduler_engine_start().
 */
void scheduler_application_register_push_hooks(void);

#ifdef __cplusplus
}
#endif
