#pragma once

/**
 * @file ai_config.h
 * @brief AI 服务商模型配置与系统提示词管理接口
 *
 * 管理 AI 提供商（ai_provider_t，支持 OpenAI、DeepSeek、Qwen、Ollama 等）、
 * 模型列表、API Key、Base URL、上下文对话轮数上限及全局 System Prompt（ai_config_t）。
 * 提供 JSON 序列化、文件加载与保存功能。
 */

#include <stdint.h>

/**
 * @struct ai_provider_t
 * @brief 单个 AI 提供商接入配置
 */
typedef struct {
    char   id[64];        /**< 服务商唯一标识（如 "openai", "deepseek", "ollama"） */
    char   name[128];     /**< 服务商显示名称（如 "DeepSeek AI", "OpenAI"） */
    char   api_key[512];  /**< API 请求密钥 */
    char   base_url[512]; /**< API 基础请求路径（如 "https://api.deepseek.com/v1"） */
    char** models;        /**< 动态字符串指针数组：该服务商下支持的模型列表 */
    int    model_count;   /**< 支持的模型数量 */
} ai_provider_t;

/**
 * @struct ai_config_t
 * @brief 全局 AI 模块配置模型
 */
typedef struct {
    ai_provider_t* providers;            /**< 动态数组：已配置的 AI 服务商列表 */
    int            provider_count;       /**< 服务商总数量 */
    char           default_provider[64]; /**< 默认选用的服务商 ID */
    char           default_model[128];   /**< 默认选用的模型标识 */
    int            context_size;         /**< 上下文对话历史携带的最大消息条数（默认 20） */
    char           system_prompt[2048];  /**< 全局系统提示词预设 */
} ai_config_t;

/**
 * @brief 从 JSON 字符串中反序列化加载 AI 配置
 *
 * 解析 JSON 字符串并动态分配 providers 及其 models 数组内存。
 *
 * @param[in] json 包含 AI 配置的 JSON 字符串，不可为 NULL
 * @param[out] out 接收反序列化结果的 ai_config_t 结构体指针
 *
 * @return int 状态码
 * @retval 0 解析成功
 * @retval -1 解析失败（JSON 语法错误或内存分配失败）
 *
 * @note 内存所有权：成功加载后，out 结构体内部包含动态分配的内存，不再使用时必须调用 ai_config_free() 释放。
 * @note 线程安全性：纯数据解析，线程安全。
 */
int ai_config_load_json(const char* json, ai_config_t* out);

/**
 * @brief 从指定文件系统路径读取并加载 AI 配置文件
 *
 * @param[in] path 配置文件路径（如 "config/ai.json"）
 * @param[out] out 接收加载结果的 ai_config_t 结构体指针
 *
 * @return int 状态码
 * @retval 0 加载成功
 * @retval -1 文件打开失败或内容解析失败
 *
 * @note 内存所有权：成功后需通过 ai_config_free() 释放动态分配的内存。
 */
int ai_config_load(const char* path, ai_config_t* out);

/**
 * @brief 将当前 AI 配置持久化保存为 JSON 文件
 *
 * 自动创建文件所在的上级目录，并将 ai_config_t 序列化为格式化 JSON 写入目标文件。
 *
 * @param[in] path 目标输出文件路径
 * @param[in] cfg 待保存的 AI 配置结构体指针，不可为 NULL
 *
 * @return int 状态码
 * @retval 0 保存成功
 * @retval -1 序列化失败或文件无法写入
 *
 * @note 线程安全性：非原子性文件写入。
 */
int ai_config_save(const char* path, const ai_config_t* cfg);

/**
 * @brief 释放 ai_config_t 结构体内部动态申请的所有堆内存
 *
 * 遍历释放所有服务商的 models 字符串数组以及 providers 数组本身。
 *
 * @param[in,out] cfg 待释放的 AI 配置结构体指针，可为 NULL
 */
void ai_config_free(ai_config_t* cfg);

/**
 * @brief 根据服务商 ID 检索匹配的服务商配置指针
 *
 * @param[in] cfg AI 配置结构体指针
 * @param[in] provider_id 待查找的服务商 ID（如 "openai"）
 *
 * @return ai_provider_t* 命中则返回对应的服务商指针；若未找到则返回 NULL
 *
 * @note 内存所有权：返回指向 cfg 内部数组元素的指针，其生命周期与 cfg 绑定。
 * @note 线程安全性：只读查找，线程安全。
 */
ai_provider_t* ai_config_find_provider(const ai_config_t* cfg, const char* provider_id);

/**
 * @brief 获取当前配置中默认选中的服务商配置指针
 *
 * 内部通过 ai_config_find_provider(cfg, cfg->default_provider) 进行查找。
 *
 * @param[in] cfg AI 配置结构体指针
 *
 * @return ai_provider_t* 默认服务商指针；若未配置或未找到则返回 NULL
 *
 * @note 内存所有权：生命周期与 cfg 绑定。
 */
ai_provider_t* ai_config_default_provider(const ai_config_t* cfg);
