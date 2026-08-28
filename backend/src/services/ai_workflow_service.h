#pragma once
#include "csilk/csilk.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* step_id;
    const char* title;
    const char* description;
    char* (*execute)(csilk_db_pool_t* pool, int64_t user_id, const csilk_json_t* params, const char* context_json);
} ai_workflow_step_t;

typedef struct {
    const char*        id;
    const char*        title;
    const char*        description;
    const char*        icon;
    int                step_count;
    ai_workflow_step_t steps[6];
} ai_workflow_def_t;

/**
 * @brief Get list of all registered workflows as JSON.
 */
csilk_json_t* ai_workflow_get_definitions_json(void);

/**
 * @brief Execute a financial workflow and stream SSE events to client.
 */
void ai_workflow_run_handler(csilk_ctx_t* c);

#ifdef __cplusplus
}
#endif
