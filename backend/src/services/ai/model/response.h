#pragma once
#include "csilk/csilk.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char* id;
    char* name;
    char* arguments;
} ai_model_tool_call_t;

typedef struct {
    char*                 content;
    ai_model_tool_call_t* tool_calls;
    int                   tool_call_count;
    int                   prompt_tokens;
    int                   completion_tokens;
    int                   total_tokens;
    char*                 finish_reason;
} ai_model_response_t;

/**
 * @brief 解析大模型完整的 Chat Completion 响应报文
 * @param json_str 模型返回的原始 JSON 响应
 * @return 构造好的响应对象（通过 ai_model_response_free 释放）
 */
ai_model_response_t* ai_model_parse_response(const char* json_str);

/**
 * @brief 释放响应对象
 */
void ai_model_response_free(ai_model_response_t* resp);

#ifdef __cplusplus
}
#endif
