#pragma once
#include "csilk/csilk.h"
#include "services/ai/policy/permission.h"
#include "services/ai/policy/risk.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char* (*ai_tool_handler_fn)(csilk_db_pool_t*    pool,
                                    int64_t             user_id,
                                    const csilk_json_t* args);

typedef struct {
    const char*           name;
    const char*           description;
    csilk_json_t*         parameters_schema;
    ai_tool_handler_fn    handler;
    ai_permission_level_t perm_level;
    ai_risk_level_t       risk_level;
} ai_tool_def_t;

/**
 * @brief 初始化工具注册表
 */
void ai_tool_registry_init(void);

/**
 * @brief 注册一个工具定义
 */
int ai_tool_registry_register(const ai_tool_def_t* tool);

/**
 * @brief 按名称查找工具定义
 */
const ai_tool_def_t* ai_tool_registry_find(const char* name);

/**
 * @brief 获取所有注册的工具定义（转为 csilk_ai_tool_t 数组）
 */
const csilk_ai_tool_t* ai_tool_registry_get_csilk_tools(size_t* count);

#ifdef __cplusplus
}
#endif
