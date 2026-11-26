/**
 * @file loop_convergence.c
 * @brief AI Runtime 主循环：Agent 循环状态机、预算收敛调用、部分文本持久化、
 *        同步执行入口 (ai_runtime_execute / ai_runtime_run_loop)。
 */
#include "services/ai/runtime/loop_internal.h"
#include "services/ai/runtime/loop.h"
#include "services/ai/runtime/context.h"
#include "services/ai/runtime/fast_json.h"
#include "services/ai/runtime/session.h"
#include "services/ai/memory/memory.h"
#include "services/ai/memory/summary.h"
#include "services/ai/policy/policy.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/registry.h"
#include "services/ai/tools/mcp/mcp_bridge.h"
#include "services/ai_tools.h"
#include "services/ai_service.h"
#include "domain/ai/rules.h"
#include "infrastructure/repositories/ai_session_repo_impl.h"
#include "infrastructure/repositories/skill_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* -------------------------------------------------------------------------- */
/* 预算收敛调用                                                                 */
/* -------------------------------------------------------------------------- */

bool
loop_execute_budget_wrap(csilk_db_pool_t*              pool,
                         ai_runtime_context_t*         ctx,
                         csilk_ai_t*                   ai_inst,
                         const char*                   model,
                         csilk_ai_message_t*           msgs,
                         size_t                        mc,
                         int                           http_timeout_ms,
                         double                        eff_price_in,
                         double                        eff_price_out,
                         loop_stream_bridge_t*         bridge,
                         csilk_json_t*                 tc_record,
                         const ai_runtime_callbacks_t* cbs,
                         void*                         user_data,
                         ai_runtime_status_t*          status)
{
    if (bridge->accumulated) {
        bridge->accumulated[0] = '\0';
        bridge->accumulated_len = 0;
    }
    csilk_ai_chat_request_t wrap_req = {
        .model = model,
        .messages = msgs,
        .message_count = mc,
        .stream = (cbs && cbs->on_text_chunk) ? 1 : 0,
        .on_chunk = loop_stream_on_text_chunk,
        .on_reasoning_chunk = loop_stream_on_thinking_chunk,
        .user_data = bridge,
        .tools = NULL,
        .tool_count = 0,
        .tool_choice = "none",
        .has_enable_thinking = ctx->has_enable_thinking,
        .enable_thinking = ctx->enable_thinking,
        .timeout_ms = http_timeout_ms,
    };
    csilk_ai_chat_response_t wrap_res = {0};
    int                      wrap_rc = csilk_ai_chat(ai_inst, &wrap_req, &wrap_res);
    double                   wrap_cost = 0.0;
    mf_ai_rule_calculate_token_cost(wrap_res.prompt_tokens,
                                    wrap_res.completion_tokens,
                                    eff_price_in,
                                    eff_price_out,
                                    &wrap_cost);
    ai_runtime_limits_record_tokens(
        &ctx->stats, wrap_res.prompt_tokens, wrap_res.completion_tokens, wrap_cost);
    const char* wrap_text = wrap_res.content ? wrap_res.content : bridge->accumulated;
    bool        success = false;
    if (wrap_rc == 0 && wrap_text && wrap_text[0]) {
        snprintf(ctx->stats.finish_reason, sizeof(ctx->stats.finish_reason), "budget");
        ai_runtime_status_set(status, AI_RUNTIME_ERR_OK, "OK", NULL);
        loop_persist_assistant_message(
            pool,
            ctx->session_id,
            model,
            wrap_text,
            (bridge->thinking_accumulated && bridge->thinking_accumulated[0])
                ? bridge->thinking_accumulated
                : NULL,
            tc_record,
            cbs,
            user_data);
        success = true;
    }
    csilk_ai_chat_response_free(&wrap_res);
    return success;
}

/* -------------------------------------------------------------------------- */
/* 主 Agent 循环                                                                */
/* -------------------------------------------------------------------------- */

ai_runtime_status_t
ai_runtime_execute_stream(csilk_db_pool_t*              pool,
                          ai_runtime_context_t*         ctx,
                          const ai_runtime_callbacks_t* cbs,
                          void*                         user_data)
{
    ai_runtime_status_t status = {0};

    /* 1. 环境准备 (参数验证、大模型驱动初始化、工具组装、消息准备、链路追踪初始化) */
    loop_exec_env_t env;
    status = loop_prepare_env(ctx, cbs, user_data, &env);
    if (status.code != AI_RUNTIME_ERR_OK) {
        return status;
    }

    loop_stream_bridge_t bridge = {
        .cbs = cbs,
        .user_data = user_data,
        .cancel_token = ctx->cancel_token,
        .accumulated = NULL,
        .accumulated_len = 0,
        .accumulated_cap = 0,
        .thinking_accumulated = NULL,
        .thinking_accumulated_len = 0,
        .thinking_accumulated_cap = 0,
    };

    /* 工具调用持久化记录 */
    csilk_json_t* tc_record = csilk_json_array();

    struct timespec start_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    int max_turns = (ctx->limits.max_iterations > 0) ? ctx->limits.max_iterations : 10;
    int got_text = 0;
    int model_call_failed = 0;
    snprintf(ctx->stats.finish_reason, sizeof(ctx->stats.finish_reason), "stop");

    /* 单次运行内同参工具调用去重表 */
    uint64_t seen_hashes[LOOP_DUP_CALL_SLOTS];
    size_t   seen_count = 0;

    /* Agent 循环核心状态机 */
    while (!got_text && ctx->stats.iterations_done < max_turns) {
        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        int64_t elapsed_ms = (now_ts.tv_sec - start_ts.tv_sec) * 1000 +
                             (now_ts.tv_nsec - start_ts.tv_nsec) / 1000000;
        ctx->stats.elapsed_ms = elapsed_ms;

        /* A. 轮次前预算与取消检查 */
        if (!ai_runtime_limits_check_pre_turn(
                &ctx->limits, &ctx->stats, ctx->cancel_token, elapsed_ms, &status)) {
            break;
        }

        /* 重置流式累积缓冲区 */
        if (bridge.accumulated) {
            bridge.accumulated[0] = '\0';
            bridge.accumulated_len = 0;
        }

        /* B. 构建大模型调用请求 */
        csilk_ai_chat_request_t req = {
            .model = env.model,
            .messages = env.msgs,
            .message_count = env.mc,
            .stream = (cbs && cbs->on_text_chunk) ? 1 : 0,
            .on_chunk = loop_stream_on_text_chunk,
            .on_reasoning_chunk = loop_stream_on_thinking_chunk,
            .user_data = &bridge,
            .tools = (csilk_ai_tool_t*)env.tools,
            .tool_count = env.tool_count,
            .tool_choice = "auto",
            .has_enable_thinking = ctx->has_enable_thinking,
            .enable_thinking = ctx->enable_thinking,
            .timeout_ms = env.http_timeout_ms,
        };

        csilk_ai_chat_response_t ai_res = {0};
        int                      rc = csilk_ai_chat(env.ai_inst, &req, &ai_res);

        /* Runtime 层兜底重试：仅当本轮尚未向客户端流出正文时生效 */
        for (int attempt = 0; rc != 0 && attempt < 2; attempt++) {
            const char* emsg =
                (ai_res.error_message && ai_res.error_message[0]) ? ai_res.error_message : "";
            if (bridge.accumulated_len > 0 || strstr(emsg, "400") || strstr(emsg, "401") ||
                strstr(emsg, "403") || strstr(emsg, "404") || strstr(emsg, "CURL error") ||
                strstr(emsg, "429") || strstr(emsg, "502") || strstr(emsg, "503") ||
                (ctx->cancel_token && *(ctx->cancel_token))) {
                break;
            }
            csilk_ai_chat_response_free(&ai_res);
            memset(&ai_res, 0, sizeof(ai_res));
            struct timespec backoff = {.tv_sec = 0,
                                       .tv_nsec = (attempt == 0 ? 500L : 1500L) * 1000000L};
            nanosleep(&backoff, NULL);
            rc = csilk_ai_chat(env.ai_inst, &req, &ai_res);
        }

        /* 记录 Token 消耗与费用 */
        double step_cost = 0.0;
        mf_ai_rule_calculate_token_cost(ai_res.prompt_tokens,
                                        ai_res.completion_tokens,
                                        env.eff_price_in,
                                        env.eff_price_out,
                                        &step_cost);
        ai_runtime_limits_record_tokens(
            &ctx->stats, ai_res.prompt_tokens, ai_res.completion_tokens, step_cost);

        /* 客户端断开 → 协作取消 */
        if (ctx->cancel_token && *(ctx->cancel_token)) {
            ai_runtime_status_set(&status,
                                  AI_RUNTIME_ERR_CANCELLED,
                                  "Cancelled by client disconnect",
                                  "SSE write failed; client closed the stream");
            csilk_ai_chat_response_free(&ai_res);
            break;
        }

        if (rc != 0) {
            const char* emsg = (ai_res.error_message && ai_res.error_message[0])
                                   ? ai_res.error_message
                                   : "AI model invocation error";
            int         timed_out = (strstr(emsg, "CURL error (28)") != NULL) ||
                                    (strstr(emsg, "Timeout was reached") != NULL);
            ai_runtime_status_set(&status,
                                  timed_out ? AI_RUNTIME_ERR_TIMEOUT : AI_RUNTIME_ERR_MODEL,
                                  timed_out ? "AI model request timed out"
                                            : "AI model request failed",
                                  emsg);
            model_call_failed = 1;
            csilk_ai_chat_response_free(&ai_res);
            break;
        }

        /* 触发异步摘要 */
        ai_summary_maybe_trigger(pool, ctx);

        /* C. 模型输出最终文本（无后续工具调用） */
        if (ai_res.tool_call_count == 0) {
            const char* final_text =
                ai_res.content ? ai_res.content : (bridge.accumulated ? bridge.accumulated : "");
            const char* final_thinking =
                (bridge.thinking_accumulated && bridge.thinking_accumulated[0])
                    ? bridge.thinking_accumulated
                    : (ai_res.reasoning_content ? ai_res.reasoning_content : NULL);
            loop_persist_assistant_message(pool,
                                           ctx->session_id,
                                           env.model,
                                           final_text,
                                           final_thinking,
                                           tc_record,
                                           cbs,
                                           user_data);
            ai_runtime_status_set(&status, AI_RUNTIME_ERR_OK, "OK", NULL);
            csilk_ai_chat_response_free(&ai_res);
            got_text = 1;
            break;
        }

        /* D. 处理工具调用（Function Calling） */
        if (!ai_runtime_limits_check_tool_budget(
                &ctx->limits, &ctx->stats, (int)ai_res.tool_call_count, &status)) {
            csilk_ai_chat_response_free(&ai_res);
            break;
        }

        /* 追加 assistant 消息（包含 tool_calls） */
        size_t       n_calls = ai_res.tool_call_count;
        const size_t asst_idx = loop_append_assistant_tool_message(
            &env.msgs, &env.mc, ai_res.content, ai_res.tool_calls, n_calls);

        /* 检查当前批次是否全为无状态网络只读工具 */
        const char* tool_names[8];
        bool        batch_eligible = (n_calls > 1 && n_calls <= 8);
        if (batch_eligible) {
            for (size_t t = 0; t < n_calls; t++) {
                tool_names[t] = env.msgs[asst_idx].tool_calls[t].name;
            }
            batch_eligible = ai_parallel_batch_eligible(tool_names, n_calls);
        }

        if (batch_eligible) {
            loop_dispatch_parallel_tools(ctx,
                                         env.msgs[asst_idx].tool_calls,
                                         n_calls,
                                         env.trace,
                                         cbs,
                                         user_data,
                                         tc_record,
                                         seen_hashes,
                                         &seen_count,
                                         &env.msgs,
                                         &env.mc);
        } else {
            loop_dispatch_sequential_tools(pool,
                                           ctx,
                                           env.msgs[asst_idx].tool_calls,
                                           n_calls,
                                           env.trace,
                                           cbs,
                                           user_data,
                                           tc_record,
                                           seen_hashes,
                                           &seen_count,
                                           &env.msgs,
                                           &env.mc);
        }

        csilk_ai_chat_response_free(&ai_res);
        ctx->stats.iterations_done++;
    }

    if (!got_text && status.code == AI_RUNTIME_ERR_OK) {
        ai_runtime_status_set(&status,
                              AI_RUNTIME_ERR_TIMEOUT,
                              "Execution finished without final text response",
                              "Max turns reached");
    }

    /* 预算/轮数耗尽 → 追加一次无工具收敛调用 */
    if (!got_text && !model_call_failed &&
        (status.code == AI_RUNTIME_ERR_TIMEOUT || status.code == AI_RUNTIME_ERR_TOOL ||
         status.code == AI_RUNTIME_ERR_CONTEXT_OVERFLOW) &&
        !(ctx->cancel_token && *(ctx->cancel_token))) {
        if (loop_execute_budget_wrap(pool,
                                     ctx,
                                     env.ai_inst,
                                     env.model,
                                     env.msgs,
                                     env.mc,
                                     env.http_timeout_ms,
                                     env.eff_price_in,
                                     env.eff_price_out,
                                     &bridge,
                                     tc_record,
                                     cbs,
                                     user_data,
                                     &status)) {
            got_text = 1;
        }
    }

    /* 被中断/超时轮次的部分 assistant 文本与思考过程持久化 */
    if (!got_text && pool && ctx->session_id > 0 &&
        ((bridge.accumulated && bridge.accumulated[0]) ||
         (bridge.thinking_accumulated && bridge.thinking_accumulated[0])) &&
        (status.code != AI_RUNTIME_ERR_MODEL || (bridge.accumulated && bridge.accumulated[0]))) {
        const char* final_text =
            (bridge.accumulated && bridge.accumulated[0]) ? bridge.accumulated : "[思考已中断]";
        loop_persist_assistant_message(pool,
                                       ctx->session_id,
                                       env.model,
                                       final_text,
                                       bridge.thinking_accumulated,
                                       tc_record,
                                       cbs,
                                       user_data);
        got_text = 1;
    }

    /* 链路追踪结算 */
    loop_finalize_trace(env.trace,
                        env.auto_created_trace,
                        pool,
                        env.eff_price_in,
                        env.eff_price_out,
                        ctx->stats.prompt_tokens,
                        ctx->stats.completion_tokens,
                        &status);

    /* 终态通知 */
    loop_notify_final_status(&status, &ctx->stats, cbs, user_data);

    /* 资源清理 */
    if (bridge.accumulated) {
        free(bridge.accumulated);
    }
    if (bridge.thinking_accumulated) {
        free(bridge.thinking_accumulated);
    }
    csilk_json_free(tc_record);
    loop_cleanup_env(&env);

    return status;
}

/* -------------------------------------------------------------------------- */
/* 同步执行入口                                                                 */
/* -------------------------------------------------------------------------- */

typedef struct {
    char*  content;
    size_t len;
    size_t cap;
} exec_sync_collector_t;

static void
exec_sync_chunk(const char* chunk, void* udata)
{
    if (!chunk || !udata) {
        return;
    }
    exec_sync_collector_t* c = (exec_sync_collector_t*)udata;
    size_t                 clen = strlen(chunk);
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

typedef struct {
    exec_sync_collector_t*        coll;
    const ai_runtime_callbacks_t* extra;
    void*                         extra_udata;
} exec_extra_ctx_t;

static void
exec_extra_text(const char* chunk, void* udata)
{
    exec_extra_ctx_t* e = (exec_extra_ctx_t*)udata;
    exec_sync_chunk(chunk, e->coll);
    if (e->extra && e->extra->on_text_chunk) {
        e->extra->on_text_chunk(chunk, e->extra_udata);
    }
}

static void
exec_extra_thinking(const char* chunk, void* udata)
{
    exec_extra_ctx_t* e = (exec_extra_ctx_t*)udata;
    if (e->extra && e->extra->on_thinking_chunk) {
        e->extra->on_thinking_chunk(chunk, e->extra_udata);
    }
}

static void
exec_extra_tool_call(const char* id, const char* name, const char* args_json, void* udata)
{
    exec_extra_ctx_t* e = (exec_extra_ctx_t*)udata;
    if (e->extra && e->extra->on_tool_call) {
        e->extra->on_tool_call(id, name, args_json, e->extra_udata);
    }
}

static void
exec_extra_tool_result(const char* id, const char* name, const char* result_json, void* udata)
{
    exec_extra_ctx_t* e = (exec_extra_ctx_t*)udata;
    if (e->extra && e->extra->on_tool_result) {
        e->extra->on_tool_result(id, name, result_json, e->extra_udata);
    }
}

static void
exec_extra_error(const ai_runtime_status_t* status, void* udata)
{
    exec_extra_ctx_t* e = (exec_extra_ctx_t*)udata;
    if (e->extra && e->extra->on_error) {
        e->extra->on_error(status, e->extra_udata);
    }
}

static void
exec_extra_done(const ai_runtime_stats_t* stats, void* udata)
{
    exec_extra_ctx_t* e = (exec_extra_ctx_t*)udata;
    if (e->extra && e->extra->on_done) {
        e->extra->on_done(stats, e->extra_udata);
    }
}

static void
exec_extra_persisted(const char* role, int64_t message_id, void* udata)
{
    exec_extra_ctx_t* e = (exec_extra_ctx_t*)udata;
    if (e->extra && e->extra->on_message_persisted) {
        e->extra->on_message_persisted(role, message_id, e->extra_udata);
    }
}

ai_runtime_result_t
ai_runtime_execute_with_callbacks(csilk_db_pool_t*              pool,
                                  ai_runtime_context_t*         ctx,
                                  const ai_runtime_callbacks_t* extra_cbs,
                                  void*                         extra_udata,
                                  ai_trace_t**                  out_trace)
{
    ai_runtime_result_t   res = {0};
    exec_sync_collector_t coll = {0};
    exec_extra_ctx_t      wrapper = {&coll, extra_cbs, extra_udata};

    ai_runtime_callbacks_t cbs = {
        .on_text_chunk = exec_extra_text,
        .on_thinking_chunk = exec_extra_thinking,
        .on_tool_call = exec_extra_tool_call,
        .on_tool_result = exec_extra_tool_result,
        .on_error = exec_extra_error,
        .on_done = exec_extra_done,
        .on_message_persisted = exec_extra_persisted,
    };

    ai_trace_t* pre_created = NULL;
    if (out_trace && ctx && !ctx->trace) {
        pre_created = ai_trace_create(ctx->user_id, ctx->session_id);
        if (pre_created) {
            ctx->trace = pre_created;
        }
    }

    res.status = ai_runtime_execute_stream(pool, ctx, &cbs, &wrapper);
    res.stats = ctx ? ctx->stats : (ai_runtime_stats_t){0};
    if (res.status.code == AI_RUNTIME_ERR_OK) {
        res.final_content = coll.content;
    } else {
        if (coll.content) {
            free(coll.content);
        }
        res.final_content = NULL;
    }

    if (out_trace) {
        if (pre_created) {
            *out_trace = pre_created;
            ctx->trace = NULL;
        } else if (ctx) {
            *out_trace = ctx->trace;
        }
    }

    return res;
}

ai_runtime_result_t
ai_runtime_execute(csilk_db_pool_t* pool, ai_runtime_context_t* ctx, ai_trace_t** out_trace)
{
    return ai_runtime_execute_with_callbacks(pool, ctx, NULL, NULL, out_trace);
}

/* -------------------------------------------------------------------------- */
/* 兼容旧签名入口                                                               */
/* -------------------------------------------------------------------------- */

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
    if (opts->provider_id) {
        strncpy(ctx.provider_id, opts->provider_id, sizeof(ctx.provider_id) - 1);
    }
    if (opts->model_name) {
        strncpy(ctx.model_name, opts->model_name, sizeof(ctx.model_name) - 1);
    }
    if (opts->max_turns > 0) {
        ctx.limits.max_iterations = opts->max_turns;
    }
    ctx.trace = trace;

    ai_config_t*  cfg = ai_config_acquire();
    csilk_json_t* hist =
        (pool && opts->session_id > 0) ? mf_ai_message_recent(pool, opts->session_id, 20) : NULL;
    if (pool && opts->session_id > 0) {
        ai_summary_flush_pending(pool, opts->session_id);
    }
    char* summary =
        (pool && opts->session_id > 0) ? mf_ai_summary_get(pool, opts->session_id) : NULL;
    csilk_json_free(ctx.messages);
    ctx.messages = ai_memory_build_messages(
        cfg ? cfg->system_prompt : NULL, hist, opts->user_prompt, 20, summary);
    ai_config_release(cfg);
    free(summary);
    if (hist) {
        csilk_json_free(hist);
    }

    ai_runtime_result_t res = ai_runtime_execute(pool, &ctx, NULL);
    ai_runtime_context_free(&ctx);

    return res.final_content;
}
