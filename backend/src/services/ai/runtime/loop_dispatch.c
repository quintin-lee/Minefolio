/**
 * @file loop_dispatch.c
 * @brief AI Runtime 工具调度层：并行/串行分发、policy 评估、同参去重、
 *        assistant 消息追加、无状态网络工具判定。
 */
#include "services/ai/runtime/loop_internal.h"
#include "services/ai/runtime/loop.h"
#include "services/ai/policy/policy.h"
#include "services/ai/policy/risk.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/registry.h"
#include "services/ai_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* -------------------------------------------------------------------------- */
/* 同参去重辅助                                                                */
/* -------------------------------------------------------------------------- */

static uint64_t
loop_call_hash(const char* name, const char* args)
{
    uint64_t    h = 1469598103934665603ULL; /* FNV-1a 64bit */
    const char* p = name ? name : "";
    while (*p) {
        h ^= (unsigned char)*p++;
        h *= 1099511628211ULL;
    }
    h ^= 0x1f;
    h *= 1099511628211ULL;
    p = args ? args : "";
    while (*p) {
        h ^= (unsigned char)*p++;
        h *= 1099511628211ULL;
    }
    return h;
}

static int
loop_call_seen(const uint64_t* hashes, size_t count, uint64_t h)
{
    for (size_t i = 0; i < count; i++) {
        if (hashes[i] == h) {
            return 1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* 无状态网络工具判定                                                          */
/* -------------------------------------------------------------------------- */

bool
is_stateless_network_tool(const char* name)
{
    if (!name) {
        return false;
    }
    return (strcmp(name, "web_search") == 0 || strcmp(name, "fetch_webpage") == 0);
}

bool
ai_parallel_batch_eligible(const char* const* names, size_t count)
{
    if (!names || count == 0 || count > 8) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (!is_stateless_network_tool(names[i])) {
            return false;
        }
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* 并行 Worker                                                                 */
/* -------------------------------------------------------------------------- */

static void*
ai_parallel_tool_worker(void* arg)
{
    ai_parallel_task_t* task = (ai_parallel_task_t*)arg;
    struct timespec     t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    task->result_str = ai_tools_execute_parsed_cancel(NULL,
                                                      task->user_id,
                                                      task->session_id,
                                                      task->args,
                                                      task->tool_name,
                                                      task->cancel_token,
                                                      task->metadata);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    task->span_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    task->executed = true;
    task->success = (task->result_str != NULL);
    if (!task->result_str) {
        task->result_str = strdup("{\"error\":\"tool execution failed\"}");
    }
    return NULL;
}

/* 双工具并行批次执行（测试与高层辅助） */
int
ai_tools_execute_parallel_batch(int64_t             user_id,
                                int64_t             session_id,
                                const char*         name1,
                                const csilk_json_t* a1,
                                const char*         name2,
                                const csilk_json_t* a2,
                                char**              out1,
                                char**              out2)
{
    const char* names[2] = {name1, name2};
    if (!ai_parallel_batch_eligible(names, 2)) {
        if (out1) {
            *out1 = ai_tools_execute_parsed_cancel(
                NULL, user_id, session_id, (csilk_json_t*)a1, name1, NULL, NULL);
        }
        if (out2) {
            *out2 = ai_tools_execute_parsed_cancel(
                NULL, user_id, session_id, (csilk_json_t*)a2, name2, NULL, NULL);
        }
        return 0;
    }
    ai_parallel_task_t tasks[2];
    memset(tasks, 0, sizeof(tasks));
    tasks[0].user_id = user_id;
    tasks[0].session_id = session_id;
    tasks[0].tool_name = (char*)name1;
    tasks[0].args = (csilk_json_t*)a1;
    tasks[1].user_id = user_id;
    tasks[1].session_id = session_id;
    tasks[1].tool_name = (char*)name2;
    tasks[1].args = (csilk_json_t*)a2;

    pthread_t threads[2];
    bool      started[2] = {false, false};
    if (pthread_create(&threads[0], NULL, ai_parallel_tool_worker, &tasks[0]) == 0) {
        started[0] = true;
    } else {
        ai_parallel_tool_worker(&tasks[0]);
    }
    if (pthread_create(&threads[1], NULL, ai_parallel_tool_worker, &tasks[1]) == 0) {
        started[1] = true;
    } else {
        ai_parallel_tool_worker(&tasks[1]);
    }
    if (started[0]) {
        pthread_join(threads[0], NULL);
    }
    if (started[1]) {
        pthread_join(threads[1], NULL);
    }

    if (out1) {
        *out1 = tasks[0].result_str;
    } else {
        free(tasks[0].result_str);
    }
    if (out2) {
        *out2 = tasks[1].result_str;
    } else {
        free(tasks[1].result_str);
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Assistant 消息追加                                                           */
/* -------------------------------------------------------------------------- */

size_t
loop_append_assistant_tool_message(csilk_ai_message_t**  msgs_ptr,
                                   size_t*               mc_ptr,
                                   const char*           content,
                                   csilk_ai_tool_call_t* tool_calls,
                                   size_t                tool_call_count)
{
    if (!msgs_ptr || !mc_ptr || (tool_call_count > 0 && !tool_calls)) {
        return (size_t)-1;
    }
    if (*mc_ptr > (SIZE_MAX / sizeof(csilk_ai_message_t)) - 1) {
        return (size_t)-1;
    }
    size_t               mc = *mc_ptr + 1;
    csilk_ai_message_t* new_msgs =
        (csilk_ai_message_t*)realloc(*msgs_ptr, sizeof(csilk_ai_message_t) * mc);
    if (!new_msgs) {
        return (size_t)-1;
    }
    *msgs_ptr = new_msgs;

    char*                 msg_content = content ? strdup(content) : strdup("");
    csilk_ai_tool_call_t* calls =
        (tool_call_count > 0) ? (csilk_ai_tool_call_t*)calloc(tool_call_count,
                                                              sizeof(csilk_ai_tool_call_t))
                              : NULL;
    if (!msg_content || (tool_call_count > 0 && !calls)) {
        free(msg_content);
        free(calls);
        return (size_t)-1;
    }

    for (size_t j = 0; j < tool_call_count; j++) {
        calls[j].id = tool_calls[j].id ? strdup(tool_calls[j].id) : strdup("");
        calls[j].name = tool_calls[j].name ? strdup(tool_calls[j].name) : strdup("");
        calls[j].arguments =
            tool_calls[j].arguments ? strdup(tool_calls[j].arguments) : strdup("{}");
        if (!calls[j].id || !calls[j].name || !calls[j].arguments) {
            for (size_t k = 0; k <= j; k++) {
                free(calls[k].id);
                free(calls[k].name);
                free(calls[k].arguments);
            }
            free(calls);
            free(msg_content);
            return (size_t)-1;
        }
    }

    (*msgs_ptr)[mc - 1] = (csilk_ai_message_t){
        .role = "assistant",
        .content = msg_content,
        .tool_call_id = NULL,
        .tool_call_count = tool_call_count,
        .tool_calls = calls,
    };
    *mc_ptr = mc;
    return mc - 1; /* asst_idx */
}

/* -------------------------------------------------------------------------- */
/* 并行工具调度                                                                 */
/* -------------------------------------------------------------------------- */

#define LOOP_MAX_PARALLEL_TOOLS 8

void
loop_dispatch_parallel_tools(ai_runtime_context_t*         ctx,
                             csilk_ai_tool_call_t*         tool_calls,
                             size_t                        n_calls,
                             ai_trace_t*                   trace,
                             const ai_runtime_callbacks_t* cbs,
                             void*                         user_data,
                             csilk_json_t*                 tc_record,
                             uint64_t*                     seen_hashes,
                             size_t*                       seen_count,
                             csilk_ai_message_t**          msgs_ptr,
                             size_t*                       mc_ptr)
{
    if (!ctx || !tool_calls || n_calls == 0 || !msgs_ptr || !mc_ptr) {
        return;
    }

    for (size_t offset = 0; offset < n_calls; offset += LOOP_MAX_PARALLEL_TOOLS) {
        size_t chunk_calls = n_calls - offset;
        if (chunk_calls > LOOP_MAX_PARALLEL_TOOLS) {
            chunk_calls = LOOP_MAX_PARALLEL_TOOLS;
        }

        ai_parallel_task_t tasks[LOOP_MAX_PARALLEL_TOOLS];
        memset(tasks, 0, sizeof(tasks));

        /* 1. 主线程保序预评估 (Pre-evaluation & Policy Check) */
        for (size_t t = 0; t < chunk_calls; t++) {
            csilk_ai_tool_call_t* tc = &tool_calls[offset + t];
            tasks[t].call_idx = offset + t;
            tasks[t].user_id = ctx->user_id;
            tasks[t].session_id = ctx->session_id;
            tasks[t].cancel_token = ctx->cancel_token;
            tasks[t].metadata = ctx->metadata;
            strncpy(
                tasks[t].tool_call_id, tc->id ? tc->id : "", sizeof(tasks[t].tool_call_id) - 1);
            tasks[t].tool_name = strdup(tc->name ? tc->name : "");
            tasks[t].raw_arguments = strdup(tc->arguments ? tc->arguments : "{}");
            tasks[t].args =
                tasks[t].raw_arguments ? csilk_json_parse(tasks[t].raw_arguments) : NULL;
            if (!tasks[t].args) {
                tasks[t].args = csilk_json_object();
            }

            if (cbs && cbs->on_tool_call) {
                cbs->on_tool_call(tc->id, tc->name, tc->arguments, user_data);
            }

            const ai_tool_t* reg_tool = tasks[t].tool_name ? ai_tool_find(tasks[t].tool_name) : NULL;
            if (reg_tool && reg_tool->internal_only) {
                tasks[t].result_str =
                    strdup("{\"error\":\"internal_only_tool\",\"reason\":\"confirm_* tools cannot "
                           "be invoked directly by model\"}");
                tasks[t].executed = false;
                continue;
            }

            ai_policy_decision_t* decision = ai_policy_evaluate(ctx->user_id,
                                                                ctx->session_id,
                                                                tasks[t].tool_name,
                                                                tasks[t].args,
                                                                AI_RISK_NO_OVERRIDE);

            if (decision && !decision->allowed) {
                csilk_json_t* err_obj = csilk_json_object();
                csilk_json_add_string(err_obj, "error", "policy_denied");
                csilk_json_add_string(err_obj,
                                      "reason",
                                      decision->reason[0] ? decision->reason
                                                          : "Tool blocked by policy");
                size_t elen = 0;
                tasks[t].result_str = csilk_json_serialize(err_obj, &elen);
                csilk_json_free(err_obj);
                tasks[t].executed = false;
            } else if (decision && decision->requires_confirmation &&
                       !ai_tool_requires_id(tasks[t].tool_name)) {
                tasks[t].result_str =
                    strdup("{\"error\":\"confirmation_required\",\"note\":\"this action needs "
                           "explicit user approval before it can run\"}");
                tasks[t].executed = false;
            } else {
                uint64_t call_hash = loop_call_hash(tasks[t].tool_name, tasks[t].raw_arguments);
                if (loop_call_seen(seen_hashes, *seen_count, call_hash)) {
                    tasks[t].result_str =
                        strdup("{\"error\":\"duplicate_call\",\"note\":\"identical tool call "
                               "already executed in this run; reuse the previous result or "
                               "answer directly\"}");
                    tasks[t].executed = false;
                } else {
                    if (*seen_count < LOOP_DUP_CALL_SLOTS) {
                        seen_hashes[(*seen_count)++] = call_hash;
                    }
                }
            }
            if (decision) {
                ai_policy_decision_free(decision);
            }
        }

        /* 2. 并行 Worker 执行（堆分配线程句柄，避免 8×8MB 栈帧虚占） */
        pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * chunk_calls);
        bool*      spawned = (bool*)calloc(chunk_calls, sizeof(bool));
        if (!threads || !spawned) {
            /* 分配失败降级为串行执行 */
            free(threads);
            free(spawned);
            threads = NULL;
            spawned = NULL;
            for (size_t t = 0; t < chunk_calls; t++) {
                if (!tasks[t].result_str) {
                    ai_parallel_tool_worker(&tasks[t]);
                }
            }
            goto parallel_done;
        }
        for (size_t t = 0; t < chunk_calls; t++) {
            if (!tasks[t].result_str) {
                if (pthread_create(&threads[t], NULL, ai_parallel_tool_worker, &tasks[t]) == 0) {
                    spawned[t] = true;
                } else {
                    ai_parallel_tool_worker(&tasks[t]);
                }
            }
        }
        for (size_t t = 0; t < chunk_calls; t++) {
            if (spawned[t]) {
                pthread_join(threads[t], NULL);
            }
        }
    parallel_done:
        free(threads);
        free(spawned);

        /* 3. 主线程保序汇聚屏障 */
        for (size_t t = 0; t < chunk_calls; t++) {
            if (tasks[t].executed) {
                ai_runtime_limits_record_tool_call(&ctx->stats, 1);
            }
            if (trace) {
                ai_trace_add_tool_span(trace,
                                       tasks[t].tool_name ? tasks[t].tool_name : "",
                                       tasks[t].span_ms,
                                       tasks[t].result_str ? strlen(tasks[t].result_str) : 0,
                                       tasks[t].success);
            }
            if (cbs && cbs->on_tool_result) {
                cbs->on_tool_result(tasks[t].tool_call_id,
                                    tasks[t].tool_name,
                                    tasks[t].result_str ? tasks[t].result_str : "{}",
                                    user_data);
            }
            csilk_json_t* rec = csilk_json_object();
            if (rec) {
                csilk_json_add_string(rec, "id", tasks[t].tool_call_id);
                csilk_json_add_string(rec, "name", tasks[t].tool_name ? tasks[t].tool_name : "");
                csilk_json_add_string(
                    rec, "arguments", tasks[t].raw_arguments ? tasks[t].raw_arguments : "");
                csilk_json_add_string(
                    rec, "result", tasks[t].result_str ? tasks[t].result_str : "");
                csilk_json_array_append(tc_record, rec);
            }

            char* clamped = loop_clamp_tool_result_export(tasks[t].result_str);
            char* isolated = ai_sanitize_untrusted_content(tasks[t].tool_name, clamped);
            free(clamped);

            char*  tc_id_dup = strdup(tasks[t].tool_call_id);
            size_t mc = *mc_ptr + 1;
            csilk_ai_message_t* new_msgs =
                (csilk_ai_message_t*)realloc(*msgs_ptr, sizeof(csilk_ai_message_t) * mc);
            if (new_msgs && tc_id_dup && isolated) {
                *msgs_ptr = new_msgs;
                (*msgs_ptr)[mc - 1] = (csilk_ai_message_t){
                    .role = "tool",
                    .content = isolated,
                    .tool_call_id = tc_id_dup,
                    .tool_calls = NULL,
                    .tool_call_count = 0,
                };
                *mc_ptr = mc;
            } else {
                if (new_msgs) {
                    *msgs_ptr = new_msgs;
                }
                free(isolated);
                free(tc_id_dup);
            }

            csilk_json_free(tasks[t].args);
            free(tasks[t].tool_name);
            free(tasks[t].raw_arguments);
            free(tasks[t].result_str);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 串行工具调度                                                                 */
/* -------------------------------------------------------------------------- */

void
loop_dispatch_sequential_tools(csilk_db_pool_t*              pool,
                               ai_runtime_context_t*         ctx,
                               csilk_ai_tool_call_t*         tool_calls,
                               size_t                        n_calls,
                               ai_trace_t*                   trace,
                               const ai_runtime_callbacks_t* cbs,
                               void*                         user_data,
                               csilk_json_t*                 tc_record,
                               uint64_t*                     seen_hashes,
                               size_t*                       seen_count,
                               csilk_ai_message_t**          msgs_ptr,
                               size_t*                       mc_ptr)
{
    for (size_t t = 0; t < n_calls; t++) {
        csilk_ai_tool_call_t* tc = &tool_calls[t];
        char*                 tc_id_copy = strdup(tc->id && tc->id[0] ? tc->id : "");
        if (cbs && cbs->on_tool_call) {
            cbs->on_tool_call(tc->id, tc->name, tc->arguments, user_data);
        }

        csilk_json_t* args = csilk_json_parse(tc->arguments ? tc->arguments : "{}");
        if (!args) {
            args = csilk_json_object();
        }

        ai_policy_decision_t* decision =
            ai_policy_evaluate(ctx->user_id, ctx->session_id, tc->name, args, AI_RISK_NO_OVERRIDE);
        char*           result_str = NULL;
        int             tool_success = 0;
        int             executed = 0;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        if (decision && !decision->allowed) {
            csilk_json_t* err_obj = csilk_json_object();
            csilk_json_add_string(err_obj, "error", "policy_denied");
            csilk_json_add_string(err_obj,
                                  "reason",
                                  decision->reason[0] ? decision->reason
                                                      : "Tool blocked by policy");
            if (decision->requires_confirmation && decision->draft) {
                csilk_json_add_string(err_obj, "confirmation_required", "true");
                csilk_json_add_string(err_obj, "draft_id", decision->draft->draft_id);
            }
            size_t elen = 0;
            result_str = csilk_json_serialize(err_obj, &elen);
            csilk_json_free(err_obj);
        } else {
            const ai_tool_t* reg_tool = ai_tool_find(tc->name);
            uint64_t         call_hash = loop_call_hash(tc->name, tc->arguments);
            if (reg_tool && reg_tool->internal_only) {
                result_str =
                    strdup("{\"error\":\"internal_only_tool\",\"reason\":\"confirm_* tools cannot "
                           "be invoked directly by model\"}");
                executed = 0;
            } else if (loop_call_seen(seen_hashes, *seen_count, call_hash)) {
                result_str = strdup("{\"error\":\"duplicate_call\",\"note\":\"identical tool call "
                                    "already executed in this run; reuse the previous result or "
                                    "answer directly\"}");
            } else if (decision && decision->requires_confirmation &&
                       !ai_tool_requires_id(tc->name)) {
                result_str =
                    strdup("{\"error\":\"confirmation_required\",\"note\":\"this action needs "
                           "explicit user approval before it can run; summarize what it would "
                           "do and ask the user to confirm, or use the propose/confirm tools\"}");
                executed = 0;
            } else {
                result_str = ai_tools_execute_parsed_cancel(pool,
                                                            ctx->user_id,
                                                            ctx->session_id,
                                                            args,
                                                            tc->name,
                                                            ctx->cancel_token,
                                                            ctx->metadata);
                executed = 1;
                tool_success = (result_str != NULL);
                if (!result_str) {
                    result_str = strdup("{\"error\":\"tool execution failed\"}");
                }
                if (*seen_count < LOOP_DUP_CALL_SLOTS) {
                    seen_hashes[(*seen_count)++] = call_hash;
                }
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &t1);
        long span_ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;

        if (decision) {
            ai_policy_decision_free(decision);
        }
        csilk_json_free(args);

        if (executed) {
            ai_runtime_limits_record_tool_call(&ctx->stats, 1);
        }
        if (trace) {
            ai_trace_add_tool_span(trace,
                                   tc->name ? tc->name : "",
                                   span_ms,
                                   result_str ? strlen(result_str) : 0,
                                   tool_success);
        }

        if (cbs && cbs->on_tool_result) {
            cbs->on_tool_result(tc->id, tc->name, result_str ? result_str : "{}", user_data);
        }

        csilk_json_t* rec = csilk_json_object();
        csilk_json_add_string(rec, "id", tc->id ? tc->id : "");
        csilk_json_add_string(rec, "name", tc->name ? tc->name : "");
        csilk_json_add_string(rec, "arguments", tc->arguments ? tc->arguments : "");
        csilk_json_add_string(rec, "result", result_str ? result_str : "");
        csilk_json_array_append(tc_record, rec);

        char* clamped = loop_clamp_tool_result_export(result_str);
        char* isolated = ai_sanitize_untrusted_content(tc->name, clamped);
        free(clamped);

        size_t              mc = *mc_ptr + 1;
        csilk_ai_message_t* new_msgs =
            (csilk_ai_message_t*)realloc(*msgs_ptr, sizeof(csilk_ai_message_t) * mc);
        if (new_msgs && tc_id_copy && isolated) {
            *msgs_ptr = new_msgs;
            (*msgs_ptr)[mc - 1] = (csilk_ai_message_t){
                .role = "tool",
                .content = isolated,
                .tool_call_id = tc_id_copy,
                .tool_calls = NULL,
                .tool_call_count = 0,
            };
            *mc_ptr = mc;
        } else {
            if (new_msgs) {
                *msgs_ptr = new_msgs;
            }
            free(isolated);
            free(tc_id_copy);
        }

        free(result_str);
    }
}
