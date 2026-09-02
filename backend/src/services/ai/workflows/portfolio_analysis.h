#pragma once
#include "services/ai/workflow/graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取“投资组合再平衡体检”工作流图定义 (wf_portfolio_rebalance)
 */
const ai_workflow_graph_t* ai_workflow_portfolio_rebalance_get_graph(void);

#ifdef __cplusplus
}
#endif
