#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t     user_id;
    int64_t     log_id;
    const char* kind; /* "scheduler" or "test" */
    const char* title;
    const char* content;
    int64_t     ttl_sec;
    int64_t*    out_push_id; /* may be NULL */
    char*       err_buf;
    size_t      err_cap;
} scheduler_push_enqueue_args_t;

typedef struct {
    /* Enqueue a push into the iLink outbox (replaces im_push_usecase_enqueue). */
    int (*enqueue)(void* pool, const scheduler_push_enqueue_args_t* args);
    /* Expire stale outbox rows (replaces im_push_usecase_expire_stale). */
    int (*expire_stale)(void* pool);
    /* Seed built-in scheduled jobs (replaces scheduler_usecase_seed_builtin_jobs). */
    int (*seed_builtin_jobs)(void* pool);
} scheduler_push_hooks_t;

/**
 * Register push delivery hooks. Must be called before scheduler_engine_start().
 * When NULL, the engine operates without iLink push support (webhook-only).
 */
void scheduler_engine_register_push_hooks(const scheduler_push_hooks_t* hooks);

/**
 * Start the background scheduler engine.
 * Spawns worker pool and timer thread.
 */
int scheduler_engine_start(void* pool);

/**
 * Gracefully stop the scheduler engine and wait for running tasks.
 */
void scheduler_engine_stop(void);

/**
 * Returns true if the scheduler engine is currently running.
 */
bool scheduler_engine_is_running(void);

/**
 * Trigger a specific job immediately in the worker pool (manual invoke or event-driven).
 */
int scheduler_engine_trigger_job_now(int64_t job_id, int64_t user_id, const char* reason);

/**
 * Notify the engine that jobs have been added/modified, triggering next run recalculations.
 */
void scheduler_engine_notify_reload(void);

/**
 * Returns the sleep duration (in seconds) computed in the last scheduler loop iteration.
 */
int scheduler_engine_get_last_sleep_sec(void);

/**
 * Returns the current count of concurrently running AI scheduled jobs.
 */
int scheduler_engine_get_ai_jobs_running(void);

#ifdef __cplusplus
}
#endif
