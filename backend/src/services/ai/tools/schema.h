#pragma once
#include "csilk/csilk.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建顶层对象类型的 JSON Schema
 */
csilk_json_t* ai_schema_create_object(void);

/**
 * @brief 向 Schema 添加基础字段属性描述
 * @param schema Schema 对象
 * @param name 字段名
 * @param type 类型 (string, number, integer, boolean, array, object)
 * @param description 描述信息
 */
void ai_schema_add_prop(csilk_json_t* schema,
                        const char*   name,
                        const char*   type,
                        const char*   description);

/**
 * @brief 向 Schema 添加枚举值字段描述
 */
void ai_schema_add_prop_enum(csilk_json_t* schema,
                             const char*   name,
                             const char*   description,
                             const char**  enum_values,
                             size_t        enum_count);

/**
 * @brief 向 Schema 添加必填字段列表
 */
void ai_schema_add_required(csilk_json_t* schema, const char* name);

#ifdef __cplusplus
}
#endif
