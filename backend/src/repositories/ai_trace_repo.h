#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include "common/ai_trace.h"

csilk_json_t* ai_trace_list(csilk_db_pool_t* pool, int64_t user_id, int64_t page,
                             int64_t page_size, const char* provider, const char* model,
                             int64_t* total);

csilk_json_t* ai_trace_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

csilk_json_t* ai_trace_stats(csilk_db_pool_t* pool, int64_t user_id);
