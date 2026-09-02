#pragma once
#include "csilk/csilk.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char*   model;
    csilk_json_t* messages;
    bool          stream;
    double        temperature;
    bool          enable_tools;
} ai_model_request_params_t;

/**
 * @brief 构建符合 OpenAI 兼容标准的 Chat Completion 请求 JSON 对象
 * @param params 请求参数定义
 * @return 构造出的 csilk_json_t* 请求体（调用方负责 free）
 */
csilk_json_t* ai_model_build_request_json(const ai_model_request_params_t* params);

#ifdef __cplusplus
}
#endif
