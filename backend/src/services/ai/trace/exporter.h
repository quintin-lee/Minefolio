#pragma once
#include "csilk/csilk.h"
#include "common/ai_trace.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 持久化 Trace 记录至数据库仓储
 */
int ai_trace_export_to_db(csilk_db_pool_t* pool, ai_trace_t* trace);

/**
 * @brief 导出用户的 AI 统计摘要 JSON
 */
csilk_json_t* ai_trace_export_stats(csilk_db_pool_t* pool, int64_t user_id);

#ifdef __cplusplus
}
#endif
