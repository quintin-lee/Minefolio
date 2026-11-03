#pragma once
#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 对只读查询工具的原始输出执行面向大模型的语义精简投影
 * @param tool_name 工具名称 (如 "get_assets", "get_daily_expenses", "get_transactions")
 * @param raw_json 工具执行器返回的原生 JSON 字符串
 * @return char* 紧凑投影后的 JSON 字符串（调用方负责 free）
 */
char* ai_project_tool_result(const char* tool_name, const char* raw_json);

#ifdef __cplusplus
}
#endif
