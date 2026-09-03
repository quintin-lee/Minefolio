#pragma once
#include "services/ai/tools/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册所有现金流与宏观概览相关 AI 工具
 */
void ai_tool_cashflow_register_all(void);

#ifdef __cplusplus
}
#endif
