#pragma once

/**
 * @file entity.h
 * @brief Async task domain constants and pure value helpers (zero external dependencies).
 */

#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MF_TASK_STATUS_PENDING = 0,
    MF_TASK_STATUS_RUNNING = 1,
    MF_TASK_STATUS_COMPLETED = 2,
    MF_TASK_STATUS_FAILED = 3
} mf_task_status_t;

static inline const char*
mf_task_status_str(mf_task_status_t status)
{
    switch (status) {
    case MF_TASK_STATUS_PENDING:
        return "pending";
    case MF_TASK_STATUS_RUNNING:
        return "running";
    case MF_TASK_STATUS_COMPLETED:
        return "completed";
    case MF_TASK_STATUS_FAILED:
        return "failed";
    default:
        return "pending";
    }
}

static inline int
mf_task_clamp_progress(int progress)
{
    if (progress < 0) {
        return 0;
    }
    if (progress > 100) {
        return 100;
    }
    return progress;
}

static inline bool
mf_task_is_terminal_status(const char* status)
{
    if (!status) {
        return false;
    }
    return strcmp(status, "completed") == 0 || strcmp(status, "failed") == 0;
}

#ifdef __cplusplus
}
#endif
