#pragma once
#include "domain/scheduler/repository.h"

int64_t       scheduler_repo_count_active(void* pool, int64_t user_id);
int64_t       scheduler_repo_count_runs_today(void* pool, int64_t user_id, const char* today_start);
int           scheduler_repo_get_next_run(void*       pool,
                                          int64_t     user_id,
                                          const char* now_str,
                                          char*       out_name,
                                          size_t      name_sz,
                                          char*       out_time,
                                          size_t      time_sz);
csilk_json_t* scheduler_repo_list_enabled_by_trigger(void* pool, const char* trigger_type);
