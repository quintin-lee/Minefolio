#include "services/ai/tools/context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

ai_tool_context_t*
ai_tool_context_create(csilk_db_pool_t* pool,
                       int64_t          user_id,
                       int64_t          session_id,
                       const char*      trace_id)
{
    ai_tool_context_t* ctx = (ai_tool_context_t*)calloc(1, sizeof(ai_tool_context_t));
    if (!ctx) {
        return NULL;
    }
    ctx->pool = pool;
    ctx->user_id = user_id;
    ctx->session_id = session_id;
    if (trace_id && trace_id[0]) {
        strncpy(ctx->trace_id, trace_id, sizeof(ctx->trace_id) - 1);
    } else {
        snprintf(ctx->trace_id,
                 sizeof(ctx->trace_id),
                 "trc_%ld_%lld",
                 (long)time(NULL),
                 (long long)user_id);
    }
    ctx->permissions = 0xFFFFFFFF; /* Default full user permissions */
    strncpy(ctx->locale, "zh-CN", sizeof(ctx->locale) - 1);
    strncpy(ctx->timezone, "Asia/Shanghai", sizeof(ctx->timezone) - 1);
    ctx->metadata = csilk_json_object();
    return ctx;
}

void
ai_tool_context_free(ai_tool_context_t* ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->metadata) {
        csilk_json_free(ctx->metadata);
    }
    free(ctx);
}
