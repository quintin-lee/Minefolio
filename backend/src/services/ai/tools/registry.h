#pragma once
#include "csilk/csilk.h"
#include "services/ai/policy/permission.h"
#include "services/ai/policy/risk.h"
#include "services/ai/tools/context.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ai_tool_s ai_tool_t;

/**
 * @brief 自定义参数业务逻辑校验函数指针
 */
typedef int (*ai_tool_validate_fn)(const ai_tool_t*    tool,
                                   const csilk_json_t* args,
                                   char*               err_buf,
                                   size_t              err_sz);

/**
 * @brief 工具实际执行逻辑函数指针
 * @return 堆分配的 JSON 字符串结果（调用方负责释放）
 */
typedef char* (*ai_tool_execute_fn)(const ai_tool_t*         tool,
                                    const ai_tool_context_t* ctx,
                                    const csilk_json_t*      args);

/**
 * @struct ai_tool_s
 * @brief 统一 AI 工具模型定义
 */
struct ai_tool_s {
    const char*           name;              /**< 工具唯一标识名（如 "get_assets"） */
    const char*           description;       /**< 工具功能描述 */
    csilk_json_t*         parameters_schema; /**< 参数 JSON Schema */
    ai_permission_level_t permission;        /**< 权限要求等级 */
    ai_risk_level_t       risk;              /**< 风险等级 */
    bool                  is_mutation;       /**< 是否为金融写操作/动账 */
    ai_tool_validate_fn   validate;          /**< 业务规则校验函数（可选） */
    ai_tool_execute_fn    execute;           /**< 执行逻辑函数 */
};

/**
 * @brief 初始化工具注册表并自动注册所有系统内置工具
 */
void ai_tool_registry_init(void);

/**
 * @brief 注册工具
 */
int ai_tool_register(const ai_tool_t* tool);

/**
 * @brief 根据名称注销工具
 */
int ai_tool_unregister(const char* name);

/**
 * @brief 根据名称查找工具
 */
const ai_tool_t* ai_tool_find(const char* name);

/**
 * @brief 获取所有已注册工具列表
 */
const ai_tool_t** ai_tool_list_all(size_t* count);

/**
 * @brief 获取用于 OpenAI Function Calling 的 csilk_ai_tool_t 数组定义
 */
const csilk_ai_tool_t* ai_tool_get_csilk_definitions(size_t* count);

#ifdef __cplusplus
}
#endif
