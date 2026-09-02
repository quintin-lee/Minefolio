#pragma once
#include "csilk/csilk.h"
#include "services/ai/runtime/session.h"
#include "services/ai/trace/trace.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * @brief 执行多轮对话与工具调用核心循环 (LLM Loop & Tool Call Loop)
 * @param pool 数据库连接池
 * @param opts 循环配置选项
 * @param trace 链路追踪对象
 * @return 最终完整的回复文本（调用方负责 free 释放），失败返回 NULL
 */
char* ai_runtime_run_loop(csilk_db_pool_t* pool, const ai_loop_options_t* opts, ai_trace_t* trace);

#ifdef __cplusplus
}
#endif
