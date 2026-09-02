#pragma once
#include "common/ai_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建并初始化一个 Trace 实例
 */
ai_trace_t* ai_trace_create(int64_t user_id, int64_t session_id);

/**
 * @brief 销毁 Trace 实例
 */
void ai_trace_destroy(ai_trace_t* trace);

#ifdef __cplusplus
}
#endif
