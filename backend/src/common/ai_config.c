/**
 * @file ai_config.c
 * @brief AI 服务商模型配置与系统提示词管理实现
 *
 * 实现了 AI 模型提供商参数解析、JSON 序列化与文件持久化、
 * 堆内存分配与回收、以及提供商根据 ID 的快速查找。
 */

#include "common/ai_config.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/**
 * @brief 解析 JSON 字符串数组并转换为 C 字符串指针数组
 *
 * @param[in] arr JSON 数组对象
 * @param[out] out_ptrs 输出字符串指针数组地址
 * @param[out] out_count 输出数组长度
 */
static void
parse_string_array(const csilk_json_t* arr, char*** out_ptrs, int* out_count)
{
    if (!arr || !csilk_json_is_array(arr)) {
        *out_ptrs = NULL;
        *out_count = 0;
        return;
    }
    int n = csilk_json_array_size(arr);
    *out_ptrs = (char**)malloc(sizeof(char*) * (size_t)(n + 1));
    if (!*out_ptrs) {
        *out_count = 0;
        return;
    }
    *out_count = n;
    for (size_t i = 0; i < (size_t)n; i++) {
        const char* s = csilk_json_string_value(csilk_json_array_get(arr, i));
        (*out_ptrs)[i] = s ? strdup(s) : strdup("");
    }
    (*out_ptrs)[n] = NULL;
}

/**
 * @brief 释放由 parse_string_array 申请的字符串指针数组
 *
 * @param[in,out] arr 待释放的字符指针数组
 */
static void
free_string_array(char** arr)
{
    if (!arr) {
        return;
    }
    for (int i = 0; arr[i]; i++) {
        free(arr[i]);
    }
    free(arr);
}

/**
 * @brief 从已解析的 JSON 根对象中提取 AI 配置字段
 *
 * @param[in] root JSON 根节点
 * @param[out] out 输出配置结构体
 * @return int 0 成功，-1 内存分配失败
 */
static int
ai_config_parse_root(const csilk_json_t* root, ai_config_t* out)
{
    const csilk_json_t* prov_arr = csilk_json_get(root, "providers");
    if (prov_arr && csilk_json_is_array(prov_arr)) {
        int pc = csilk_json_array_size(prov_arr);
        out->providers = (ai_provider_t*)malloc(sizeof(ai_provider_t) * (size_t)pc);
        if (!out->providers) {
            return -1;
        }
        out->provider_count = pc;
        for (int i = 0; i < pc; i++) {
            const csilk_json_t* p = csilk_json_array_get(prov_arr, i);
            strncpy(out->providers[i].id,
                    csilk_json_get_string(p, "id") ?: "",
                    sizeof(out->providers[i].id) - 1);
            strncpy(out->providers[i].name,
                    csilk_json_get_string(p, "name") ?: "",
                    sizeof(out->providers[i].name) - 1);
            strncpy(out->providers[i].api_key,
                    csilk_json_get_string(p, "api_key") ?: "",
                    sizeof(out->providers[i].api_key) - 1);
            strncpy(out->providers[i].base_url,
                    csilk_json_get_string(p, "base_url") ?: "",
                    sizeof(out->providers[i].base_url) - 1);
            parse_string_array(csilk_json_get(p, "models"),
                               &out->providers[i].models,
                               &out->providers[i].model_count);
        }
    }

    const char* dp = csilk_json_get_string(root, "default_provider");
    if (dp) {
        strncpy(out->default_provider, dp, sizeof(out->default_provider) - 1);
    }
    const char* dm = csilk_json_get_string(root, "default_model");
    if (dm) {
        strncpy(out->default_model, dm, sizeof(out->default_model) - 1);
    }
    const csilk_json_t* cs_val = csilk_json_get(root, "context_size");
    out->context_size = cs_val ? (int)csilk_json_number_value(cs_val) : 20;
    if (out->context_size < 5) {
        out->context_size = 20;
    }
    const char* sp = csilk_json_get_string(root, "system_prompt");
    if (sp) {
        strncpy(out->system_prompt, sp, sizeof(out->system_prompt) - 1);
    }
    return 0;
}

/**
 * @brief 从 JSON 字符串解析加载 AI 配置
 *
 * @param[in] json JSON 字符串
 * @param[out] out 目标配置对象
 * @return int 0 成功，-1 失败
 */
int
ai_config_load_json(const char* json, ai_config_t* out)
{
    memset(out, 0, sizeof(*out));
    if (!json || !json[0]) {
        return -1;
    }
    csilk_json_t* root = csilk_json_parse(json);
    if (!root) {
        return -1;
    }
    int rc = ai_config_parse_root(root, out);
    csilk_json_free(root);
    return rc;
}

/**
 * @brief 从文件读取加载 AI 配置
 *
 * @param[in] path 文件路径
 * @param[out] out 目标配置对象
 * @return int 0 成功，-1 失败
 */
int
ai_config_load(const char* path, ai_config_t* out)
{
    memset(out, 0, sizeof(*out));
    FILE* f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    char   buf[65536];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return ai_config_load_json(buf, out);
}

/**
 * @brief 将 AI 配置序列化写入文件
 *
 * @param[in] path 文件路径
 * @param[in] cfg 配置结构体
 * @return int 0 成功，-1 失败
 */
int
ai_config_save(const char* path, const ai_config_t* cfg)
{
    csilk_json_t* root = csilk_json_object();
    if (!root) {
        return -1;
    }

    csilk_json_t* prov_arr = csilk_json_array();
    for (int i = 0; i < cfg->provider_count; i++) {
        csilk_json_t* p = csilk_json_object();
        csilk_json_add_string(p, "id", cfg->providers[i].id);
        csilk_json_add_string(p, "name", cfg->providers[i].name);
        csilk_json_add_string(p, "api_key", cfg->providers[i].api_key);
        csilk_json_add_string(p, "base_url", cfg->providers[i].base_url);
        csilk_json_t* ml = csilk_json_array();
        for (int j = 0; j < cfg->providers[i].model_count; j++) {
            csilk_json_array_append(ml, csilk_json_string_new(cfg->providers[i].models[j]));
        }
        csilk_json_add_array(p, "models", ml);
        csilk_json_array_append(prov_arr, p);
    }
    csilk_json_add_array(root, "providers", prov_arr);
    csilk_json_add_string(root, "default_provider", cfg->default_provider);
    csilk_json_add_string(root, "default_model", cfg->default_model);
    csilk_json_add_int(root, "context_size", cfg->context_size);
    csilk_json_add_string(root, "system_prompt", cfg->system_prompt);

    size_t slen = 0;
    char*  json = csilk_json_serialize(root, &slen);
    csilk_json_free(root);
    if (!json) {
        return -1;
    }

    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char* slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }

    FILE* f = fopen(path, "w");
    if (!f) {
        free(json);
        return -1;
    }
    fwrite(json, 1, slen, f);
    fclose(f);
    free(json);
    return 0;
}

/**
 * @brief 释放 AI 配置对象持有的所有堆内存
 *
 * @param[in,out] cfg 目标配置对象
 */
void
ai_config_free(ai_config_t* cfg)
{
    if (cfg->providers) {
        for (int i = 0; i < cfg->provider_count; i++) {
            free_string_array(cfg->providers[i].models);
        }
        free(cfg->providers);
    }
}

/**
 * @brief 根据服务商 ID 获取配置指针
 *
 * @param[in] cfg 配置结构体
 * @param[in] provider_id 服务商 ID
 * @return ai_provider_t* 服务商配置指针，找不到返回 NULL
 */
ai_provider_t*
ai_config_find_provider(const ai_config_t* cfg, const char* provider_id)
{
    for (int i = 0; i < cfg->provider_count; i++) {
        if (strcmp(cfg->providers[i].id, provider_id) == 0) {
            return &cfg->providers[i];
        }
    }
    return NULL;
}

/**
 * @brief 获取当前默认服务商配置
 *
 * @param[in] cfg 配置结构体
 * @return ai_provider_t* 默认服务商配置指针，未找到返回 NULL
 */
ai_provider_t*
ai_config_default_provider(const ai_config_t* cfg)
{
    return ai_config_find_provider(cfg, cfg->default_provider);
}
