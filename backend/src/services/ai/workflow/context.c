#include "services/ai/workflow/context.h"
#include <stdlib.h>
#include <string.h>

ai_wf_context_t*
ai_wf_context_create(csilk_db_pool_t*    pool,
                     int64_t             user_id,
                     int64_t             session_id,
                     const csilk_json_t* params)
{
    ai_wf_context_t* ctx = (ai_wf_context_t*)calloc(1, sizeof(ai_wf_context_t));
    if (!ctx) {
        return NULL;
    }
    ctx->pool = pool;
    ctx->user_id = user_id;
    ctx->session_id = session_id;
    ctx->params = params ? csilk_json_copy(params) : csilk_json_object();
    ctx->shared_state = csilk_json_object();
    return ctx;
}

void
ai_wf_context_free(ai_wf_context_t* ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->params) {
        csilk_json_free(ctx->params);
    }
    if (ctx->shared_state) {
        csilk_json_free(ctx->shared_state);
    }
    free(ctx);
}

void
ai_wf_context_set(ai_wf_context_t* ctx, const char* key, const char* value)
{
    if (!ctx || !ctx->shared_state || !key) {
        return;
    }
    csilk_json_add_string(ctx->shared_state, key, value ? value : "");
}

const char*
ai_wf_context_get(const ai_wf_context_t* ctx, const char* key)
{
    if (!ctx || !ctx->shared_state || !key) {
        return NULL;
    }
    return csilk_json_get_string(ctx->shared_state, key);
}
