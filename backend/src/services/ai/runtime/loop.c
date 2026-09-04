#include "services/ai/runtime/loop.h"
#include "services/ai/runtime/context.h"
#include "services/ai/runtime/session.h"
#include "services/ai/memory/memory.h"
#include "services/ai/policy/policy.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/registry.h"
#include "services/ai_tools.h"
#include "services/ai_service.h"
#include "domain/ai/rules.h"
#include "repositories/ai_session_repo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    const ai_runtime_callbacks_t* cbs;
    void*                         user_data;
    char*                         accumulated;
    size_t                        accumulated_len;
    size_t                        accumulated_cap;
} loop_stream_bridge_t;

static void
on_chunk_bridge(const char* chunk, void* user_data)
{
    if (!chunk || !user_data) {
        return;
    }
    loop_stream_bridge_t* b = (loop_stream_bridge_t*)user_data;
    size_t clen = strlen(chunk);
    if (b->accumulated_len + clen + 1 > b->accumulated_cap) {
        size_t new_cap = (b->accumulated_cap == 0) ? 1024 : (b->accumulated_cap * 2 + clen);
        char*  new_buf = (char*)realloc(b->accumulated, new_cap);
        if (new_buf) {
            b->accumulated = new_buf;
            b->accumulated_cap = new_cap;
        }
    }
    if (b->accumulated && b->accumulated_len + clen + 1 <= b->accumulated_cap) {
        memcpy(b->accumulated + b->accumulated_len, chunk, clen);
        b->accumulated_len += clen;
        b->accumulated[b->accumulated_len] = '\0';
    }
    if (b->cbs && b->cbs->on_text_chunk) {
        b->cbs->on_text_chunk(chunk, b->user_data);
    }
}

ai_runtime_status_t
ai_runtime_execute_stream(csilk_db_pool_t*              pool,
                          ai_runtime_context_t*         ctx,
                          const ai_runtime_callbacks_t* cbs,
                          void*                         user_data)
{
    ai_runtime_status_t status = {0};

    /* 1. 验证上下文有效性 */
    if (!ctx) {
        ai_runtime_status_set(&status, AI_RUNTIME_ERR_VALIDATION, "Context is NULL", "ctx cannot be NULL");
        if (cbs && cbs->on_error) cbs->on_error(&status, user_data);
        return status;
    }

    if (ctx->cancel_token && *(ctx->cancel_token)) {
        ai_runtime_status_set(&status, AI_RUNTIME_ERR_CANCELLED, "Execution cancelled by client", "Cancellation token signaled");
        if (cbs && cbs->on_error) cbs->on_error(&status, user_data);
        return status;
    }

    if (ctx->user_id <= 0) {
        ai_runtime_status_set(&status, AI_RUNTIME_ERR_VALIDATION, "Invalid user context", "user_id must be > 0");
        if (cbs && cbs->on_error) cbs->on_error(&status, user_data);
        return status;
    }

    /* 2. 匹配大模型服务商与模型配置 */
    ai_config_t*   cfg = ai_get_config();
    ai_provider_t* prov = NULL;
    if (ctx->provider_id[0]) {
        prov = ai_config_find_provider(cfg, ctx->provider_id);
    }
    if (!prov && cfg) {
        prov = ai_config_default_provider(cfg);
    }

    if (!prov || (!prov->api_key[0] && strcmp(prov->id, "ollama") != 0)) {
        ai_runtime_status_set(&status, AI_RUNTIME_ERR_MODEL, "No available AI provider", "Provider not configured or missing API key");
        if (cbs && cbs->on_error) cbs->on_error(&status, user_data);
        return status;
    }

    const char* model = ctx->model_name[0]
                            ? ctx->model_name
                            : ((prov->models[0] && prov->models[0][0]) ? prov->models[0] : (cfg ? cfg->default_model : "default"));

    /* 3. 构造 AI 驱动实例 */
    const char* dname = (strcmp(prov->id, "ollama") == 0) ? "ollama" : "openai";
    const char* key = (prov->api_key[0] != '\0') ? prov->api_key : "dummy";
    csilk_ai_t* ai_inst = csilk_ai_new(dname, key, prov->base_url[0] ? prov->base_url : NULL);
    if (!ai_inst) {
        ai_runtime_status_set(&status, AI_RUNTIME_ERR_MODEL, "Failed to create AI driver instance", "csilk_ai_new returned NULL");
        if (cbs && cbs->on_error) cbs->on_error(&status, user_data);
        return status;
    }

    /* 4. 解析可用工具集 */
    size_t                 tool_count = ctx->tool_count;
    const csilk_ai_tool_t* tools = ctx->tools;
    if (!tools && tool_count == 0) {
        tools = ai_tools_get_definitions(&tool_count);
    }

    /* 5. 组装初始消息结构数组 */
    size_t json_msg_count = ctx->messages ? csilk_json_array_size(ctx->messages) : 0;
    size_t mc = json_msg_count;
    csilk_ai_message_t* msgs = NULL;
    if (mc > 0) {
        msgs = (csilk_ai_message_t*)calloc(mc, sizeof(csilk_ai_message_t));
        for (size_t i = 0; i < mc; i++) {
            csilk_json_t* item = csilk_json_array_get(ctx->messages, i);
            msgs[i] = (csilk_ai_message_t){
                .role = csilk_json_get_string(item, "role"),
                .content = csilk_json_get_string(item, "content"),
            };
        }
    } else {
        mc = 1;
        msgs = (csilk_ai_message_t*)calloc(mc, sizeof(csilk_ai_message_t));
        msgs[0] = (csilk_ai_message_t){
            .role = "system",
            .content = (cfg && cfg->system_prompt[0]) ? cfg->system_prompt : "你是一个专业的个人财务与财富管理AI助手。",
        };
    }
    size_t initial_mc = mc;

    /* 6. 链路追踪初始化 */
    ai_trace_t  local_trace;
    ai_trace_t* trace = ctx->trace;
    int         created_local_trace = 0;
    if (!trace) {
        ai_trace_init(&local_trace, ctx->user_id, ctx->session_id);
        ai_trace_set_provider(&local_trace, prov->id, model);
        ai_trace_set_params(&local_trace, ctx->temperature, ctx->max_tokens, ctx->top_p);
        trace = &local_trace;
        created_local_trace = 1;
    }

    loop_stream_bridge_t bridge = {
        .cbs = cbs,
        .user_data = user_data,
        .accumulated = NULL,
        .accumulated_len = 0,
        .accumulated_cap = 0,
    };

    struct timespec start_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    int max_turns = (ctx->limits.max_iterations > 0) ? ctx->limits.max_iterations : 10;
    int got_text = 0;

    /* 7. Agent 循环核心状态机 */
    while (!got_text && ctx->stats.iterations_done < max_turns) {
        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        int64_t elapsed_ms = (now_ts.tv_sec - start_ts.tv_sec) * 1000 + (now_ts.tv_nsec - start_ts.tv_nsec) / 1000000;
        ctx->stats.elapsed_ms = elapsed_ms;

        /* A. 轮次前预算与取消检查 */
        if (!ai_runtime_limits_check_pre_turn(&ctx->limits, &ctx->stats, ctx->cancel_token, elapsed_ms, &status)) {
            break;
        }

        /* 重置流式累积缓冲区 */
        if (bridge.accumulated) {
            bridge.accumulated[0] = '\0';
            bridge.accumulated_len = 0;
        }

        /* B. 构建大模型调用请求 */
        csilk_ai_chat_request_t req = {
            .model = model,
            .messages = msgs,
            .message_count = mc,
            .stream = (cbs && cbs->on_text_chunk) ? 1 : 0,
            .on_chunk = on_chunk_bridge,
            .user_data = &bridge,
            .tools = (csilk_ai_tool_t*)tools,
            .tool_count = tool_count,
            .tool_choice = "auto",
        };

        csilk_ai_chat_response_t ai_res = {0};
        int                      rc = csilk_ai_chat(ai_inst, &req, &ai_res);

        /* 记录 Token 消耗与费用 */
        double step_cost = 0.0;
        mf_ai_rule_calculate_token_cost(ai_res.prompt_tokens, ai_res.completion_tokens, 2.5, 10.0, &step_cost);
        ai_runtime_limits_record_tokens(&ctx->stats, ai_res.prompt_tokens, ai_res.completion_tokens, step_cost);

        if (rc != 0) {
            const char* emsg = (ai_res.error_message && ai_res.error_message[0]) ? ai_res.error_message : "AI model invocation error";
            ai_runtime_status_set(&status, AI_RUNTIME_ERR_MODEL, "AI model request failed", emsg);
            csilk_ai_chat_response_free(&ai_res);
            break;
        }

        /* C. 模型输出最终文本（无后续工具调用） */
        if (ai_res.tool_call_count == 0) {
            const char* final_text = ai_res.content ? ai_res.content : (bridge.accumulated ? bridge.accumulated : "");
            if (pool && ctx->session_id > 0 && final_text && final_text[0]) {
                ai_message_insert(pool, ctx->session_id, "assistant", final_text, model);
            }
            ai_runtime_status_set(&status, AI_RUNTIME_ERR_OK, "OK", NULL);
            csilk_ai_chat_response_free(&ai_res);
            got_text = 1;
            break;
        }

        /* D. 处理工具调用（Function Calling） */
        if (!ai_runtime_limits_check_tool_budget(&ctx->limits, &ctx->stats, (int)ai_res.tool_call_count, &status)) {
            csilk_ai_chat_response_free(&ai_res);
            break;
        }

        /* 追加 assistant 消息（包含 tool_calls） */
        size_t n_calls = ai_res.tool_call_count;
        mc++;
        msgs = (csilk_ai_message_t*)realloc(msgs, sizeof(csilk_ai_message_t) * mc);
        msgs[mc - 1] = (csilk_ai_message_t){
            .role = "assistant",
            .content = ai_res.content ? strdup(ai_res.content) : strdup(""),
            .tool_call_id = NULL,
            .tool_call_count = n_calls,
            .tool_calls = (csilk_ai_tool_call_t*)calloc(n_calls, sizeof(csilk_ai_tool_call_t)),
        };
        for (size_t j = 0; j < n_calls; j++) {
            msgs[mc - 1].tool_calls[j].id = ai_res.tool_calls[j].id ? strdup(ai_res.tool_calls[j].id) : strdup("");
            msgs[mc - 1].tool_calls[j].name = ai_res.tool_calls[j].name ? strdup(ai_res.tool_calls[j].name) : strdup("");
            msgs[mc - 1].tool_calls[j].arguments = ai_res.tool_calls[j].arguments ? strdup(ai_res.tool_calls[j].arguments) : strdup("{}");
        }

        /* 逐一校验安全策略并执行工具 */
        for (size_t t = 0; t < n_calls; t++) {
            csilk_ai_tool_call_t* tc = &msgs[mc - 1].tool_calls[t];
            if (cbs && cbs->on_tool_call) {
                cbs->on_tool_call(tc->id, tc->name, tc->arguments, user_data);
            }

            csilk_json_t* args = csilk_json_parse(tc->arguments ? tc->arguments : "{}");
            if (!args) {
                args = csilk_json_object();
            }

            /* 1. 安全策略综合评估 (Policy Evaluation) */
            ai_policy_decision_t* decision = ai_policy_evaluate(ctx->user_id, ctx->session_id, tc->name, args);
            char* result_str = NULL;
            int   tool_success = 0;
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);

            if (decision && !decision->allowed) {
                /* 策略拦截或需二次确认 */
                csilk_json_t* err_obj = csilk_json_object();
                csilk_json_add_string(err_obj, "error", "policy_denied");
                csilk_json_add_string(err_obj, "reason", decision->reason[0] ? decision->reason : "Tool blocked by policy");
                if (decision->requires_confirmation && decision->draft) {
                    csilk_json_add_string(err_obj, "confirmation_required", "true");
                    csilk_json_add_string(err_obj, "draft_id", decision->draft->draft_id);
                }
                size_t elen = 0;
                result_str = csilk_json_serialize(err_obj, &elen);
                csilk_json_free(err_obj);
            } else {
                /* 2. 工具执行 (Tool Execution) */
                result_str = ai_tools_execute_parsed(pool, ctx->user_id, args, tc->name);
                tool_success = (result_str != NULL);
                if (!result_str) {
                    result_str = strdup("{\"error\":\"tool execution failed\"}");
                }
            }

            clock_gettime(CLOCK_MONOTONIC, &t1);
            long span_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;

            if (decision) {
                ai_policy_decision_free(decision);
            }
            csilk_json_free(args);

            ai_runtime_limits_record_tool_call(&ctx->stats, 1);
            if (trace) {
                ai_trace_add_tool_span(trace, tc->name ? tc->name : "", span_ms, result_str ? strlen(result_str) : 0, tool_success);
            }

            if (cbs && cbs->on_tool_result) {
                cbs->on_tool_result(tc->id, tc->name, result_str ? result_str : "{}", user_data);
            }

            /* 追加 role="tool" 消息 */
            mc++;
            msgs = (csilk_ai_message_t*)realloc(msgs, sizeof(csilk_ai_message_t) * mc);
            msgs[mc - 1] = (csilk_ai_message_t){
                .role = "tool",
                .content = result_str,
                .tool_call_id = tc->id ? strdup(tc->id) : strdup(""),
                .tool_calls = NULL,
                .tool_call_count = 0,
            };
        }

        csilk_ai_chat_response_free(&ai_res);
        ctx->stats.iterations_done++;
    }

    if (!got_text && status.code == AI_RUNTIME_ERR_OK) {
        ai_runtime_status_set(&status, AI_RUNTIME_ERR_TIMEOUT, "Execution finished without final text response", "Max turns reached");
    }

    /* 8. 链路追踪结算并保存 */
    if (trace) {
        ai_trace_calculate_tokens_and_cost(trace, ctx->stats.prompt_tokens, ctx->stats.completion_tokens);
        ai_trace_finish(trace, (status.code == AI_RUNTIME_ERR_OK) ? "ok" : "error", status.message);
        if (pool) {
            ai_trace_save(pool, trace);
        }
        if (created_local_trace) {
            ai_trace_free(trace);
        }
    }

    /* 9. 事件回调终态通知 */
    if (status.code == AI_RUNTIME_ERR_OK) {
        if (cbs && cbs->on_done) {
            cbs->on_done(&ctx->stats, user_data);
        }
    } else {
        if (cbs && cbs->on_error) {
            cbs->on_error(&status, user_data);
        }
    }

    /* 10. 资源清理 */
    csilk_ai_free(ai_inst);
    if (bridge.accumulated) {
        free(bridge.accumulated);
    }
    for (size_t i = initial_mc; i < mc; i++) {
        if (msgs[i].content) {
            free((void*)msgs[i].content);
        }
        if (msgs[i].tool_call_id) {
            free((void*)msgs[i].tool_call_id);
        }
        if (msgs[i].tool_calls) {
            for (size_t j = 0; j < msgs[i].tool_call_count; j++) {
                free(msgs[i].tool_calls[j].id);
                free(msgs[i].tool_calls[j].name);
                free(msgs[i].tool_calls[j].arguments);
            }
            free(msgs[i].tool_calls);
        }
    }
    free(msgs);

    return status;
}

typedef struct {
    char*  content;
    size_t len;
    size_t cap;
} exec_sync_collector_t;

static void
exec_sync_chunk(const char* chunk, void* udata)
{
    if (!chunk || !udata) return;
    exec_sync_collector_t* c = (exec_sync_collector_t*)udata;
    size_t clen = strlen(chunk);
    if (c->len + clen + 1 > c->cap) {
        size_t ncap = (c->cap == 0) ? 2048 : (c->cap * 2 + clen);
        char*  nbuf = (char*)realloc(c->content, ncap);
        if (nbuf) {
            c->content = nbuf;
            c->cap = ncap;
        }
    }
    if (c->content && c->len + clen + 1 <= c->cap) {
        memcpy(c->content + c->len, chunk, clen);
        c->len += clen;
        c->content[c->len] = '\0';
    }
}

ai_runtime_result_t
ai_runtime_execute(csilk_db_pool_t* pool, ai_runtime_context_t* ctx)
{
    ai_runtime_result_t   res = {0};
    exec_sync_collector_t coll = {0};

    ai_runtime_callbacks_t cbs = {
        .on_text_chunk = exec_sync_chunk,
        .on_tool_call = NULL,
        .on_tool_result = NULL,
        .on_error = NULL,
        .on_done = NULL,
    };

    res.status = ai_runtime_execute_stream(pool, ctx, &cbs, &coll);
    res.stats = ctx ? ctx->stats : (ai_runtime_stats_t){0};
    if (res.status.code == AI_RUNTIME_ERR_OK) {
        res.final_content = coll.content;
    } else {
        if (coll.content) {
            free(coll.content);
        }
        res.final_content = NULL;
    }

    return res;
}

char*
ai_runtime_run_loop(csilk_db_pool_t* pool, const ai_loop_options_t* opts, ai_trace_t* trace)
{
    if (!opts || opts->user_id <= 0) {
        return NULL;
    }

    ai_runtime_context_t ctx;
    ai_runtime_context_init(&ctx);
    ctx.user_id = opts->user_id;
    ctx.session_id = opts->session_id;
    if (opts->provider_id) strncpy(ctx.provider_id, opts->provider_id, sizeof(ctx.provider_id) - 1);
    if (opts->model_name) strncpy(ctx.model_name, opts->model_name, sizeof(ctx.model_name) - 1);
    if (opts->max_turns > 0) ctx.limits.max_iterations = opts->max_turns;
    ctx.trace = trace;

    ai_config_t* cfg = ai_get_config();
    csilk_json_t* hist = (pool && opts->session_id > 0) ? ai_message_recent(pool, opts->session_id, 20) : NULL;
    csilk_json_free(ctx.messages);
    ctx.messages = ai_memory_build_messages(cfg ? cfg->system_prompt : NULL, hist, opts->user_prompt, 20);
    if (hist) csilk_json_free(hist);

    ai_runtime_result_t res = ai_runtime_execute(pool, &ctx);
    ai_runtime_context_free(&ctx);

    return res.final_content;
}
