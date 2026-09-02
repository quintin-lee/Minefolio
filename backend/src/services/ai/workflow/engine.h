#pragma once
#include "csilk/csilk.h"
#include "services/ai/workflow/graph.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化工作流引擎注册表
 */
void ai_workflow_engine_init(void);

/**
 * @brief 注册一个工作流定义
 */
int ai_workflow_register(const ai_workflow_graph_t* graph);

/**
 * @brief 按 ID 查找工作流图定义
 */
const ai_workflow_graph_t* ai_workflow_find(const char* workflow_id);

/**
 * @brief 获取所有注册的工作流定义的 JSON 数组
 */
csilk_json_t* ai_workflow_get_all_definitions_json(void);

#ifdef __cplusplus
}
#endif
