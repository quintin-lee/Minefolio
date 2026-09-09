#pragma once

/**
 * @file ai_trace_repo_impl.h
 * @brief 基础设施层 AI 追踪仓储实现头文件
 */

#include "csilk/csilk.h"
#include "common/db.h"
#include "common/ai_trace.h"
#include <stdint.h>

csilk_json_t* mf_ai_trace_list(csilk_db_pool_t* pool,
                               int64_t          user_id,
                               int64_t          page,
                               int64_t          page_size,
                               const char*      provider,
                               const char*      model,
                               int64_t*         total);

csilk_json_t* mf_ai_trace_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

csilk_json_t* mf_ai_trace_stats(csilk_db_pool_t* pool, int64_t user_id);

int64_t mf_ai_trace_save(csilk_db_pool_t* pool, ai_trace_t* t);
