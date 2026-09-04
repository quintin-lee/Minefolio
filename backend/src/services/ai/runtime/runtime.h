#pragma once
#include "csilk/csilk.h"
#include "common/ai_config.h"
#include "services/ai/runtime/error.h"
#include "services/ai/runtime/limits.h"
#include "services/ai/runtime/context.h"
#include "services/ai/runtime/loop.h"
#include "services/ai/runtime/session.h"
#include "services/ai/memory/memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 AI 运行时子系统
 */
void ai_runtime_init(csilk_db_pool_t* pool);

/**
 * @brief 关闭 AI 运行时子系统
 */
void ai_runtime_shutdown(void);

/**
 * @brief 获取全局 AI 运行时配置
 */
ai_config_t* ai_runtime_get_config(void);

#ifdef __cplusplus
}
#endif
