#pragma once
#include "common/db.h"

int  market_scheduler_start(csilk_db_pool_t* pool);
void market_scheduler_stop(void);
void market_scheduler_set_config(bool auto_sync, int interval_min, const char* sync_mode);
void market_scheduler_get_config(bool*  out_auto_sync,
                                 int*   out_interval_min,
                                 char*  out_sync_mode,
                                 size_t mode_cap);
