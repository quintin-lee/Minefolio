#pragma once
#include "services/ai/tools/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册所有交易相关 AI 工具
 */
void ai_tool_transaction_register_all(void);

#ifdef __cplusplus
}
#endif
