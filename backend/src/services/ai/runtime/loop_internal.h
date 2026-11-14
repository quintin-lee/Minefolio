/**
 * @file loop_internal.h
 * @brief loop.c 拆分后的内部共享类型与跨模块函数声明。
 *
 * 三个实现文件：
 *   loop_env.c       — 环境准备/清理、技能 L1 prompt、消息组装、trace 结算
 *   loop_dispatch.c  — 并行/串行工具调度、policy 评估、去重、结果裁剪
 *   loop_convergence.c — 预算收敛调用、部分文本持久化、终态通知、主循环
 *
 * 本头仅在同一目录内使用，不对外暴露。
 */
#pragma once
#include "csilk/csilk.h"
#include "services/ai/runtime/loop.h"
#include "services/ai/runtime/context.h"
#include "services/ai/runtime/error.h"
#include "services/ai/runtime/limits.h"
#include "services/ai/trace/trace.h"
#include "services/ai/tools/mcp/mcp_bridge.h"
#include "services/ai_service.h"
#include "common/db.h"
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------- */
/* 常量与流式桥接                                                              */
/* -------------------------------------------------------------------------- */

/** 单次运行内同参工具调用的去重槽位上限 */
#define LOOP_DUP_CALL_SLOTS 64
/** 进入模型上下文的单条工具结果字节上限（超出仅保留前缀预览） */
#define LOOP_TOOL_RESULT_MAX_BYTES 8192
/** 单次模型 HTTP 调用总超时（含流式接收全程，默认 5 分钟） */
#define LOOP_AI_HTTP_TIMEOUT_MS 300000

typedef struct {
    const ai_runtime_callbacks_t* cbs;
    void*                         user_data;
    volatile bool*                cancel_token;
    char*                         accumulated;
    size_t                        accumulated_len;
    size_t                        accumulated_cap;
    char*                         thinking_accumulated;
    size_t                        thinking_accumulated_len;
    size_t                        thinking_accumulated_cap;
} loop_stream_bridge_t;

/* -------------------------------------------------------------------------- */
/* 环境 struct（loop_env.c 定义，各模块共享）                                   */
/* -------------------------------------------------------------------------- */

typedef struct {
    ai_config_t*           cfg;
    ai_provider_t*         prov;
    const char*            model;
    double                 eff_price_in;
    double                 eff_price_out;
    csilk_ai_t*            ai_inst;
    const csilk_ai_tool_t* tools;
    size_t                 tool_count;
    int                    mcp_merged;
    csilk_ai_message_t*    msgs;
    size_t                 mc;
    size_t                 initial_mc;
    char*                  owner_sys_prompt;
    ai_trace_t*            trace;
    int                    auto_created_trace;
    int                    http_timeout_ms;
} loop_exec_env_t;

/* -------------------------------------------------------------------------- */
/* 并行任务 struct（loop_dispatch.c 使用）                                      */
/* -------------------------------------------------------------------------- */

typedef struct {
    size_t               call_idx;
    int64_t              user_id;
    int64_t              session_id;
    char                 tool_call_id[64];
    char*                tool_name;
    char*                raw_arguments;
    csilk_json_t*        args;
    const volatile bool* cancel_token;
    char*                result_str;
    long                 span_ms;
    bool                 executed;
    bool                 success;
} ai_parallel_task_t;

/* -------------------------------------------------------------------------- */
/* loop_env.c 导出                                                            */
/* -------------------------------------------------------------------------- */

ai_runtime_status_t loop_prepare_env(ai_runtime_context_t*         ctx,
                                     const ai_runtime_callbacks_t* cbs,
                                     void*                         user_data,
                                     loop_exec_env_t*              env);

void loop_cleanup_env(loop_exec_env_t* env);

void loop_persist_assistant_message(csilk_db_pool_t*              pool,
                                    int64_t                       session_id,
                                    const char*                   model,
                                    const char*                   content,
                                    const char*                   thinking,
                                    csilk_json_t*                 tc_record,
                                    const ai_runtime_callbacks_t* cbs,
                                    void*                         user_data);

void loop_finalize_trace(ai_trace_t*          trace,
                         int                  auto_created_trace,
                         csilk_db_pool_t*     pool,
                         double               eff_price_in,
                         double               eff_price_out,
                         int64_t              prompt_tokens,
                         int64_t              completion_tokens,
                         ai_runtime_status_t* status);

void loop_notify_final_status(const ai_runtime_status_t*    status,
                              const ai_runtime_stats_t*     stats,
                              const ai_runtime_callbacks_t* cbs,
                              void*                         user_data);

/* -------------------------------------------------------------------------- */
/* loop_dispatch.c 导出                                                       */
/* -------------------------------------------------------------------------- */

/* 工具结果裁剪（UTF-8 边界安全 + JSON 合法） */
char* loop_clamp_tool_result_export(const char* result);

size_t loop_append_assistant_tool_message(csilk_ai_message_t**  msgs_ptr,
                                          size_t*               mc_ptr,
                                          const char*           content,
                                          csilk_ai_tool_call_t* tool_calls,
                                          size_t                tool_call_count);

void loop_dispatch_parallel_tools(ai_runtime_context_t*         ctx,
                                  csilk_ai_tool_call_t*         tool_calls,
                                  size_t                        n_calls,
                                  ai_trace_t*                   trace,
                                  const ai_runtime_callbacks_t* cbs,
                                  void*                         user_data,
                                  csilk_json_t*                 tc_record,
                                  uint64_t*                     seen_hashes,
                                  size_t*                       seen_count,
                                  csilk_ai_message_t**          msgs_ptr,
                                  size_t*                       mc_ptr);

void loop_dispatch_sequential_tools(csilk_db_pool_t*              pool,
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
                                    size_t*                       mc_ptr);

/* -------------------------------------------------------------------------- */
/* loop_convergence.c 导出                                                    */
/* -------------------------------------------------------------------------- */

bool loop_execute_budget_wrap(csilk_db_pool_t*              pool,
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
                              ai_runtime_status_t*          status);

/* -------------------------------------------------------------------------- */
/* 流式桥接回调（loop_env.c 实现，主循环调用）                                   */
/* -------------------------------------------------------------------------- */

void loop_stream_on_text_chunk(const char* chunk, void* user_data);

void loop_stream_on_thinking_chunk(const char* chunk, void* user_data);
