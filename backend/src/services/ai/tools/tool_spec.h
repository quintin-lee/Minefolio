#pragma once
#include "services/ai/tools/registry.h"
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AI_TOOL_MAX_PARAMS 16
#define AI_TOOL_MAX_ENUM_VALUES 12

/**
 * @brief 声明式工具参数配置
 */
typedef struct {
    const char* name;
    const char* type; /* "string", "number", "integer", "boolean", "array", "object" */
    const char* description;
    bool        required;
    const char* enum_values[AI_TOOL_MAX_ENUM_VALUES]; /* NULL 结尾；首元素为 NULL 表示普通字段 */
} ai_tool_param_spec_t;

/**
 * @brief 配置驱动的工具注册声明
 */
typedef struct {
    const char*           name;
    const char*           description;
    const char*           action_type; /* 可选：绑定 confirm_* 的 action_type */
    ai_permission_level_t permission;
    ai_risk_level_t       risk;
    bool                  is_mutation;
    bool                  requires_id;
    bool                  internal_only;
    ai_tool_validate_fn   validate;
    ai_tool_execute_fn    execute;
    const char*           raw_schema_json; /* 可选：复杂嵌套 Schema 的 JSON 原文 */
    size_t                param_count;
    ai_tool_param_spec_t  params[AI_TOOL_MAX_PARAMS];
} ai_tool_spec_t;

#define AI_TOOL_PARAM(n, t, d)                                                                     \
    {                                                                                              \
        .name = (n), .type = (t), .description = (d), .required = false                            \
    }

#define AI_TOOL_PARAM_REQ(n, t, d)                                                                 \
    {                                                                                              \
        .name = (n), .type = (t), .description = (d), .required = true                             \
    }

#define AI_TOOL_PARAM_ENUM(n, d, req, ...)                                                         \
    {                                                                                              \
        .name = (n), .type = "string", .description = (d), .required = (req),                      \
        .enum_values = {__VA_ARGS__, NULL}                                                         \
    }

/**
 * @brief 根据声明式配置构造 JSON Schema 对象
 */
csilk_json_t* ai_tool_spec_build_schema(const ai_tool_spec_t* spec);

/**
 * @brief 注册单个声明式工具配置
 */
int ai_tool_register_spec(const ai_tool_spec_t* spec);

/**
 * @brief 批量注册声明式工具配置表
 */
int ai_tool_register_specs(const ai_tool_spec_t* specs, size_t count);

#ifdef __cplusplus
}
#endif
