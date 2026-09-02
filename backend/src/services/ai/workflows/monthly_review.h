#pragma once
#include "services/ai/workflow/graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取“月末财务深度复盘”工作流图定义 (wf_monthly_review)
 */
const ai_workflow_graph_t* ai_workflow_monthly_review_get_graph(void);

#ifdef __cplusplus
}
#endif
