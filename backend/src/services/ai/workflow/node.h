#pragma once
#include "services/ai/workflow/context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef char* (*ai_wf_node_fn)(csilk_db_pool_t*    pool,
                               int64_t             user_id,
                               const csilk_json_t* params,
                               const char*         context_json);

typedef struct {
    const char*   node_id;
    const char*   title;
    const char*   description;
    ai_wf_node_fn execute;
} ai_workflow_node_t;

#ifdef __cplusplus
}
#endif
