#pragma once
#include "domain/scheduler/entity.h"
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>

/* Scheduler Domain Repository Contract */
csilk_json_t* mf_scheduler_repo_job_list(void*    pool,
                                         int64_t  user_id,
                                         bool     is_admin,
                                         int64_t  page,
                                         int64_t  page_size,
                                         int64_t* out_total);
csilk_json_t* mf_scheduler_repo_job_get(void* pool, int64_t user_id, bool is_admin, int64_t job_id);
int64_t       mf_scheduler_repo_job_create(void*       pool,
                                           int64_t     user_id,
                                           const char* name,
                                           const char* desc,
                                           const char* job_type,
                                           const char* trigger_type,
                                           const char* trigger_cfg,
                                           const char* payload_cfg,
                                           const char* delivery_channel,
                                           const char* delivery_cfg,
                                           const char* next_run_at);
int64_t       mf_scheduler_repo_job_create_builtin(void*       pool,
                                                   const char* name,
                                                   const char* desc,
                                                   const char* job_type,
                                                   const char* trigger_type,
                                                   const char* trigger_cfg,
                                                   const char* payload_cfg,
                                                   const char* delivery_channel,
                                                   const char* delivery_cfg,
                                                   const char* next_run_at);
csilk_json_t* mf_scheduler_repo_job_get_by_name(void* pool, const char* name);
bool          mf_scheduler_repo_is_builtin_enabled(void* pool, const char* handler_name);
int           mf_scheduler_repo_job_claim_cron_run(void*       pool,
                                                   int64_t     job_id,
                                                   const char* expected_next_run,
                                                   const char* new_next_run);
int           mf_scheduler_repo_job_update(void*       pool,
                                           int64_t     user_id,
                                           bool        is_admin,
                                           int64_t     job_id,
                                           const char* name,
                                           const char* desc,
                                           const char* trigger_type,
                                           const char* trigger_cfg,
                                           const char* payload_cfg,
                                           const char* delivery_channel,
                                           const char* delivery_cfg,
                                           const char* next_run_at);
int           mf_scheduler_repo_job_delete(void* pool, int64_t user_id, int64_t job_id);
int           mf_scheduler_repo_job_set_enabled(
              void* pool, int64_t user_id, bool is_admin, int64_t job_id, bool enabled);
int mf_scheduler_repo_job_update_last_run(
    void* pool, int64_t job_id, const char* status, int duration_ms, const char* next_run_at);

csilk_json_t* mf_scheduler_repo_get_user_channel(void* pool, int64_t user_id);
int           mf_scheduler_repo_save_user_channel(void*       pool,
                                                  int64_t     user_id,
                                                  const char* channel_type,
                                                  const char* channel_name,
                                                  const char* webhook_url,
                                                  const char* secret_key);

int64_t       mf_scheduler_repo_log_create(void*       pool,
                                           int64_t     job_id,
                                           int64_t     user_id,
                                           const char* trigger_reason);
int           mf_scheduler_repo_log_complete(void*       pool,
                                             int64_t     log_id,
                                             const char* status,
                                             int         duration_ms,
                                             const char* output_summary,
                                             const char* error_msg,
                                             const char* notif_status);
csilk_json_t* mf_scheduler_repo_log_list(void*    pool,
                                         int64_t  user_id,
                                         bool     is_admin,
                                         int64_t  job_id,
                                         int64_t  page,
                                         int64_t  page_size,
                                         int64_t* out_total);
