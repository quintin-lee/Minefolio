#pragma once
#include "common/ai_config.h"
#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 解析并验证请求的模型名称，若无效则返回服务商默认模型
 */
const char* ai_model_resolve_name(const ai_provider_t* provider,
                                  const ai_config_t*   cfg,
                                  const char*          requested_model);

/**
 * @brief 将服务商支持的模型列表序列化为 JSON 数组
 */
csilk_json_t* ai_model_list_to_json(const ai_provider_t* provider);

#ifdef __cplusplus
}
#endif
