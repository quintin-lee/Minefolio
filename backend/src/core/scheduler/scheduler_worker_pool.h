#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*scheduler_task_func_t)(void* arg);

typedef struct scheduler_worker_pool scheduler_worker_pool_t;

scheduler_worker_pool_t* scheduler_worker_pool_create(int num_threads);

int
scheduler_worker_pool_submit(scheduler_worker_pool_t* pool, scheduler_task_func_t func, void* arg);

void scheduler_worker_pool_destroy(scheduler_worker_pool_t* pool);

#ifdef __cplusplus
}
#endif
