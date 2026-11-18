#pragma once

#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

void api_scheduler_dashboard(csilk_ctx_t* c);
void api_scheduler_jobs_list(csilk_ctx_t* c);
void api_scheduler_jobs_create(csilk_ctx_t* c);
void api_scheduler_jobs_get(csilk_ctx_t* c);
void api_scheduler_jobs_update(csilk_ctx_t* c);
void api_scheduler_jobs_delete(csilk_ctx_t* c);
void api_scheduler_jobs_toggle(csilk_ctx_t* c);
void api_scheduler_jobs_run(csilk_ctx_t* c);
void api_scheduler_jobs_logs(csilk_ctx_t* c);
void api_scheduler_channel_get(csilk_ctx_t* c);
void api_scheduler_channel_save(csilk_ctx_t* c);
void api_scheduler_channel_test(csilk_ctx_t* c);

void register_scheduler_routes(csilk_app_t* app);

#ifdef __cplusplus
}
#endif
