#pragma once
#include "csilk/csilk.h"
#include "services/ai/workflow/graph.h"
#include "services/ai/workflow/context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 执行工作流图并通过 SSE (Server-Sent Events) 流式推送执行状态与诊断报告
 * @param c HTTP 上下文
 * @param graph 工作流图定义
 * @param ctx 执行上下文
 * @return 0 成功完成, -1 失败
 */
int
ai_workflow_execute_stream(csilk_ctx_t* c, const ai_workflow_graph_t* graph, ai_wf_context_t* ctx);

#ifdef __cplusplus
}
#endif
