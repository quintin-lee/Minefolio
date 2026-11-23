#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
