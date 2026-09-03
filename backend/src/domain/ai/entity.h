#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief AI 会话实体 (Domain AI Session Entity)
 * @note 严格禁止依赖任何外部 DB、传输协议或 JSON 框架
 */
typedef struct mf_ai_session {
    int64_t id;
    int64_t user_id;
    char    title[128];
    char    model[64];
    char    provider[64];
    char    created_at[64];
    char    updated_at[64];
} mf_ai_session_t;

/**
 * @brief AI 会话消息实体
 */
typedef struct mf_ai_message {
    int64_t id;
    int64_t session_id;
    char    role[32];
    char    content[2048];
    char    created_at[64];
} mf_ai_message_t;

/**
 * @brief AI 供应商配置模型
 */
typedef struct mf_ai_provider_config {
    char id[64];
    char name[128];
    char api_key[256];
    char base_url[256];
    char models[16][64];
    int  model_count;
} mf_ai_provider_config_t;

/**
 * @brief AI 链路追踪汇总统计实体
 */
typedef struct mf_ai_trace_summary {
    int64_t total_traces;
    int64_t total_prompt_tokens;
    int64_t total_completion_tokens;
    int64_t total_tokens;
    double  avg_latency_ms;
} mf_ai_trace_summary_t;
