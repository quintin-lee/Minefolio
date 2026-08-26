#pragma once
#include <stdint.h>

typedef struct {
    char   id[64];
    char   name[128];
    char   api_key[512];
    char   base_url[512];
    char** models;
    int    model_count;
} ai_provider_t;

typedef struct {
    ai_provider_t* providers;
    int            provider_count;
    char           default_provider[64];
    char           default_model[128];
    int            context_size;
    char           system_prompt[2048];
} ai_config_t;

/** @brief 从 JSON 字符串加载 AI 配置。返回 0 成功，-1 失败。 */
int ai_config_load_json(const char* json, ai_config_t* out);

/** @brief 从配置文件加载 AI 配置。返回 0 成功，-1 失败。 */
int ai_config_load(const char* path, ai_config_t* out);

/** @brief 保存配置到文件。返回 0 成功。 */
int ai_config_save(const char* path, const ai_config_t* cfg);

/** @brief 释放配置资源。 */
void ai_config_free(ai_config_t* cfg);

/** @brief 根据 provider_id 获取 provider 指针，找不到返回 NULL。 */
ai_provider_t* ai_config_find_provider(const ai_config_t* cfg, const char* provider_id);

/** @brief 获取当前默认 provider。 */
ai_provider_t* ai_config_default_provider(const ai_config_t* cfg);
