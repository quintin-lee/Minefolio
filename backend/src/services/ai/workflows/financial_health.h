#pragma once
#include "services/ai/workflow/graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取“财务健康分评估”工作流图定义 (wf_health_score)
 */
const ai_workflow_graph_t* ai_workflow_health_score_get_graph(void);

/**
 * @brief 获取“应急基金健康检查”工作流图定义 (wf_emergency_fund)
 */
const ai_workflow_graph_t* ai_workflow_emergency_fund_get_graph(void);

#ifdef __cplusplus
}
#endif
