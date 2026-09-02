#pragma once
#include "services/ai/workflow/node.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AI_WF_MAX_STEPS 8

typedef struct {
    const char*        id;
    const char*        title;
    const char*        description;
    const char*        icon;
    int                node_count;
    ai_workflow_node_t nodes[AI_WF_MAX_STEPS];
} ai_workflow_graph_t;

#ifdef __cplusplus
}
#endif
