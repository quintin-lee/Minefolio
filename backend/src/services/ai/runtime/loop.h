#pragma once
#include "csilk/csilk.h"
#include "services/ai/runtime/context.h"
#include "services/ai/runtime/error.h"
#include "services/ai/runtime/limits.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*on_text_chunk)(const char* chunk, void* udata);
    void (*on_tool_call)(const char* id, const char* name, const char* args_json, void* udata);
    void (*on_tool_result)(const char* id, const char* name, const char* result_json, void* udata);
    void (*on_error)(const ai_runtime_status_t* status, void* udata);
    void (*on_done)(const ai_runtime_stats_t* stats, void* udata);
} ai_runtime_callbacks_t;

typedef struct {
    char*               final_content; /**< 最终文本输出 (需调用方 free) */
    ai_runtime_status_t status;        /**< 执行状态 */
    ai_runtime_stats_t  stats;         /**< 运行指标 */
} ai_runtime_result_t;

/**
 * @brief 执行统一 AI Runtime 流式/事件驱动 Agent 循环
 * @param pool 数据库连接池（可为 NULL）
 * @param ctx 运行时上下文容器
 * @param cbs 事件回调函数集合
 * @param user_data 传递给回调的用户数据指针
 * @return ai_runtime_status_t 终态状态对象
 */
ai_runtime_status_t ai_runtime_execute_stream(csilk_db_pool_t*              pool,
                                              ai_runtime_context_t*         ctx,
                                              const ai_runtime_callbacks_t* cbs,
                                              void*                         user_data);

/**
 * @brief 执行统一 AI Runtime 同步阻塞 Agent 循环
 * @param pool 数据库连接池（可为 NULL）
 * @param ctx 运行时上下文容器
 * @return ai_runtime_result_t 包含完整输出文本与执行指标的返回对象
 */
ai_runtime_result_t ai_runtime_execute(csilk_db_pool_t* pool, ai_runtime_context_t* ctx);

/* 兼容旧签名 */
typedef void (*ai_stream_chunk_cb)(const char* chunk, void* user_data);
typedef struct {
    int64_t            user_id;
    int64_t            session_id;
    const char*        user_prompt;
    const char*        provider_id;
    const char*        model_name;
    int                max_turns;
    ai_stream_chunk_cb on_chunk;
    void*              user_data;
} ai_loop_options_t;

char* ai_runtime_run_loop(csilk_db_pool_t* pool, const ai_loop_options_t* opts, ai_trace_t* trace);

#ifdef __cplusplus
}
#endif
