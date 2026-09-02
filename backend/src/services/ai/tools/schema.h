#pragma once
#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 在属性对象中添加一个属性定义
 */
void ai_schema_add_prop(csilk_json_t* props, const char* name, const char* type, const char* desc);

/**
 * @brief 构造标准 Function Calling JSON Schema (type: object, properties, required)
 */
csilk_json_t* ai_schema_create(csilk_json_t* props, const char** required_names, int req_count);

#ifdef __cplusplus
}
#endif
