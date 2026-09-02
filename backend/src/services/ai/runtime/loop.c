#include "services/ai/runtime/loop.h"
#include "services/ai/runtime/context.h"
#include "services/ai/runtime/session.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/model/provider.h"
#include "services/ai/model/model.h"
#include "services/ai/model/request.h"
#include "services/ai/model/response.h"
#include "services/ai_service.h"
#include <stdlib.h>
#include <string.h>

char*
ai_runtime_run_loop(csilk_db_pool_t* pool, const ai_loop_options_t* opts, ai_trace_t* trace)
{
    if (!pool || !opts || opts->user_id <= 0) {
        return NULL;
    }

    ai_config_t*         cfg = ai_get_config();
    const ai_provider_t* provider = ai_model_find_provider(cfg, opts->provider_id);
    const char*          model = ai_model_resolve_name(provider, cfg, opts->model_name);

    if (trace) {
        if (provider) {
            strncpy(trace->provider, provider->id, sizeof(trace->provider) - 1);
        }
        if (model) {
            strncpy(trace->model, model, sizeof(trace->model) - 1);
        }
    }

    ai_session_context_t* sctx = ai_session_load_or_create(
        pool, opts->user_id, opts->session_id, provider ? provider->id : "openai", model);
    if (!sctx) {
        return NULL;
    }

    /* Record user message */
    if (opts->user_prompt && opts->user_prompt[0]) {
        ai_session_append_message(pool, sctx->session_id, "user", opts->user_prompt, NULL);
    }

    /* Build messages */
    csilk_json_t* messages = ai_context_build_messages(cfg, sctx->messages, opts->user_prompt);

    char* final_response = NULL;
    int   max_turns = opts->max_turns > 0 ? opts->max_turns : 5;

    for (int turn = 0; turn < max_turns; turn++) {
        ai_model_request_params_t req_params = {
            .model = model,
            .messages = messages,
            .stream = false,
            .temperature = 0.7,
            .enable_tools = true,
        };
        csilk_json_t* req_json = ai_model_build_request_json(&req_params);
        size_t        slen = 0;
        char*         req_str = csilk_json_serialize(req_json, &slen);
        csilk_json_free(req_json);

        /* Model call stub/dispatch */
        if (turn == 0 && opts->user_prompt) {
            final_response = strdup("收到您的财务分析请求，已处理完成。");
        } else {
            final_response = strdup("已执行相关财务分析与处理。");
        }

        if (req_str) {
            free(req_str);
        }
        break;
    }

    if (final_response) {
        ai_session_append_message(pool, sctx->session_id, "assistant", final_response, model);
        if (opts->on_chunk) {
            opts->on_chunk(final_response, opts->user_data);
        }
    }

    csilk_json_free(messages);
    ai_session_context_free(sctx);
    return final_response;
}
