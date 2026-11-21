#pragma once

#include "csilk/csilk.h"
#include "csilk/drivers/db.h"

#ifdef __cplusplus
extern "C" {
#endif

int  task_controller_init(csilk_db_pool_t* pool, int worker_threads);
void task_controller_shutdown(void);
void register_task_routes(csilk_app_t* app);

#ifdef __cplusplus
}
#endif
