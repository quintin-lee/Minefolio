#include "services/ai/trace/exporter.h"
#include "infrastructure/repositories/ai_trace_repo_impl.h"

int
ai_trace_export_to_db(csilk_db_pool_t* pool, ai_trace_t* trace)
{
    if (!pool || !trace) {
        return -1;
    }
    return mf_ai_trace_save(pool, trace);
}

csilk_json_t*
ai_trace_export_stats(csilk_db_pool_t* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return NULL;
    }
    return mf_ai_trace_stats(pool, user_id);
}
