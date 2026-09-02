#pragma once
#include "common/ai_config.h"
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 根据 provider id 查询对应的服务商配置
 */
const ai_provider_t* ai_model_find_provider(const ai_config_t* cfg, const char* provider_id);

/**
 * @brief 构建指定服务商的聊天补全端点 URL
 */
int ai_model_build_chat_url(const ai_provider_t* provider, char* out_url, size_t sz);

/**
 * @brief 测试指定服务商与模型的连通性
 */
int
ai_model_test_provider(const ai_provider_t* provider, const char* model, char* out_msg, size_t sz);

#ifdef __cplusplus
}
#endif
