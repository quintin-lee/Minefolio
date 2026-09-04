#include "services/ai/runtime/context.h"
#include "services/ai/memory/memory.h"
#include <stdlib.h>
#include <string.h>

void
ai_runtime_context_init(ai_runtime_context_t* ctx)
{
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(ai_runtime_context_t));
    ctx->limits = ai_runtime_limits_default();
    ctx->temperature = 0.7;
    ctx->messages = csilk_json_array();
    ctx->metadata = csilk_json_object();
}

void
ai_runtime_context_free(ai_runtime_context_t* ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->messages) {
        csilk_json_free(ctx->messages);
        ctx->messages = NULL;
    }
    if (ctx->metadata) {
        csilk_json_free(ctx->metadata);
        ctx->metadata = NULL;
    }
}

csilk_json_t*
ai_context_build_messages(const ai_config_t*  cfg,
                          const csilk_json_t* history_messages,
                          const char*         user_prompt)
{
    const char* sys = (cfg && cfg->system_prompt[0]) ? cfg->system_prompt : NULL;
    int max_hist = (cfg && cfg->context_size > 0) ? cfg->context_size : 20;
    return ai_memory_build_messages(sys, history_messages, user_prompt, max_hist);
}
