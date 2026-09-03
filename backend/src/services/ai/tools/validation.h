#pragma once
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 根据 JSON Schema 对输入参数进行严格类型和必填项校验
 *
 * @param schema 工具定义的 JSON Schema
 * @param args 待校验的输入参数对象
 * @param err_buf 输出错误信息的缓冲区
 * @param err_sz 缓冲区容量
 * @return int 校验通过返回 0，校验失败返回 -1
 */
int ai_tool_validate_args(const csilk_json_t* schema,
                          const csilk_json_t* args,
                          char*               err_buf,
                          size_t              err_sz);

/**
 * @brief 规范化工具输入参数（去除首尾空白、类型转换、注入默认值）
 *
 * @param schema 工具定义的 JSON Schema
 * @param args 原始输入参数
 * @return csilk_json_t* 规范化后的新 JSON 对象（调用方负责释放）
 */
csilk_json_t* ai_tool_normalize_args(const csilk_json_t* schema, const csilk_json_t* args);

#ifdef __cplusplus
}
#endif
