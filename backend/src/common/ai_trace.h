#pragma once

/**
 * @file ai_trace.h
 * @brief AI 会话调用全链路追踪 (Trace)、性能指标与成本审计接口
 *
 * 负责捕获并记录单次 AI 对话或工作流调用的详细生命周期指标，包括：
 * 输入消息/系统提示词序列化、首字延迟 (TTFT)、总响应耗时、Token 消耗预估与精确统计、
 * 吞吐速率 (tokens/s)、模型 API 费用核算、工具调用跨度 (Tool Spans) 以及数据库持久化。
 */

#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>
#include <time.h>

/**
 * @struct ai_trace_t
 * @brief 单次 AI 请求全链路追踪跟踪上下文
 */
typedef struct {
    int64_t         id;                /**< 追踪记录在数据库中的主键 ID */
    int64_t         user_id;           /**< 发起请求的用户 ID */
    int64_t         session_id;        /**< 关联的 AI 对话会话 ID */
    char            provider[64];      /**< 所选 AI 服务商标识（如 "openai", "deepseek"） */
    char            model[128];        /**< 所选模型名称（如 "deepseek-chat", "gpt-4o"） */
    char*           input_messages;    /**< 动态堆字符串：输入上下文消息列表的 JSON 序列化字符串 */
    char*           output_content;    /**< 动态堆字符串：模型生成的完整回复内容 */
    char*           system_prompt;     /**< 动态堆字符串：使用的系统提示词 */
    int             prompt_tokens;     /**< 输入提示词所消耗的 Token 数 */
    int             completion_tokens; /**< 模型生成内容所消耗的 Token 数 */
    int             total_tokens;      /**< 消耗的总 Token 数 (prompt + completion) */
    long            latency_ms;        /**< 请求从发起到完全结束的总耗时（毫秒） */
    long            first_token_ms;    /**< 首字延迟 TTFT (Time to First Token) 耗时（毫秒） */
    double          tokens_per_sec;    /**< 模型流式输出吞吐速率（Tokens 每秒） */
    double          cost_usd;          /**< 该次调用估算折合的花费（美元 USD） */
    double          temperature;       /**< 采样温度参数 */
    int             max_tokens;        /**< 最大生成 Token 限制 */
    double          top_p;             /**< 核采样 top_p 参数 */
    char            status[32];        /**< 请求完成状态（"ok", "error"） */
    char*           error_message;     /**< 动态堆字符串：发生错误时的详细异常信息 */
    char*           metadata;          /**< 动态堆字符串：扩展元数据 JSON 字符串 */
    struct timespec t_start;           /**< 请求开始单调时钟时间戳 */
    struct timespec t_first_token;     /**< 接收到首个流式数据块的单调时钟时间戳 */
    struct timespec t_end;             /**< 请求结束单调时钟时间戳 */
    int             has_first_token;   /**< 标记是否已记录首字到达 */
    long            accumulated_len;   /**< 累积接收到的输出字符长度 */
    char            tool_spans[3072];  /**< 固定缓冲区：工具函数调用跨度的 JSON 数组字符串 */
} ai_trace_t;

/**
 * @brief 初始化 AI 追踪上下文
 *
 * 清零结构体并分配初始堆字符串，记录开始时间戳 (CLOCK_MONOTONIC)。
 *
 * @param[out] t 待初始化的追踪结构体指针，不可为 NULL
 * @param[in] user_id 用户 ID
 * @param[in] session_id 会话 ID
 *
 * @note 内存所有权：初始化后内部包含堆分配指针，使用完毕后必须调用 ai_trace_free() 释放。
 */
void ai_trace_init(ai_trace_t* t, int64_t user_id, int64_t session_id);

/**
 * @brief 设置本次调用的服务商与模型名称
 *
 * @param[in,out] t 追踪上下文指针
 * @param[in] provider 服务商代码（如 "deepseek"）
 * @param[in] model 模型代码（如 "deepseek-chat"）
 */
void ai_trace_set_provider(ai_trace_t* t, const char* provider, const char* model);

/**
 * @brief 设置推理模型超参数
 *
 * @param[in,out] t 追踪上下文指针
 * @param[in] temperature 采样温度 (0.0 ~ 2.0)
 * @param[in] max_tokens 最大 Token 生成数
 * @param[in] top_p 核采样比例 (0.0 ~ 1.0)
 */
void ai_trace_set_params(ai_trace_t* t, double temperature, int max_tokens, double top_p);

/**
 * @brief 设置当前调用的系统提示词 (System Prompt)
 *
 * @param[in,out] t 追踪上下文指针
 * @param[in] prompt 系统提示词文本
 */
void ai_trace_set_system_prompt(ai_trace_t* t, const char* prompt);

/**
 * @brief 序列化输入对话消息列表并记录到追踪上下文中
 *
 * @param[in,out] t 追踪上下文指针
 * @param[in] messages_array 包含历史与当前消息的 JSON 数组指针
 */
void ai_trace_serialize_messages(ai_trace_t* t, csilk_json_t* messages_array);

/**
 * @brief 追加模型流式返回的内容数据块
 *
 * 自动进行 realloc 动态扩容并将 chunk 追加到 output_content 字符串末尾。
 *
 * @param[in,out] t 追踪上下文指针
 * @param[in] chunk 本次流式收到的文本片段
 */
void ai_trace_append_output(ai_trace_t* t, const char* chunk);

/**
 * @brief 记录首字生成到达时刻 (TTFT)
 *
 * 在流式首个数据块到达时调用一次，计算与起始时间 t_start 的毫秒差。
 *
 * @param[in,out] t 追踪上下文指针
 */
void ai_trace_record_first_token(ai_trace_t* t);

/**
 * @brief 根据文本内容启发式估算 Token 数量
 *
 * 规则：基于字符编码分类统计，ASCII 字符按 ~3.5 字符/Token 估算，中日韩 (CJK) 多字节字符按 ~1.2 字符/Token 估算。
 *
 * @param[in] text 待估算文本字符串
 *
 * @return int 估算得出的 Token 数量
 */
int ai_estimate_tokens_from_text(const char* text);

/**
 * @brief 核算并记录 Token 消耗量与估算花费（美元）
 *
 * 若传入的 prompt_tokens / completion_tokens 为 0，则自动调用 ai_estimate_tokens_from_text 进行文本估算。
 * 并根据模型类别（GPT-4o、DeepSeek、Qwen、Ollama 等）的每百万 Token 单价计算总成本。
 *
 * @param[in,out] t 追踪上下文指针
 * @param[in] prompt_tokens 服务端返回的输入 Token 数（若无则传 0）
 * @param[in] completion_tokens 服务端返回的输出 Token 数（若无则传 0）
 */
void ai_trace_calculate_tokens_and_cost(ai_trace_t* t, int prompt_tokens, int completion_tokens);

/**
 * @brief 结束追踪生命周期并计算总耗时与生成速率
 *
 * 采集结束时刻 t_end，计算总耗时 latency_ms，并基于有效输出耗时计算生成速率 tokens_per_sec。
 *
 * @param[in,out] t 追踪上下文指针
 * @param[in] status 完成状态字符串（"ok" 或 "error"）
 * @param[in] error 错误描述（若成功可为 NULL）
 */
void ai_trace_finish(ai_trace_t* t, const char* status, const char* error);

/**
 * @brief 将完整的链路追踪记录插入持久化数据库表 ai_traces
 *
 * @param[in] pool 数据库连接池指针
 * @param[in] t 追踪上下文指针
 *
 * @return int64_t 成功返回插入的记录主键 ID，失败返回 -1
 */
int64_t ai_trace_save(csilk_db_pool_t* pool, ai_trace_t* t);

/**
 * @brief 释放追踪上下文占用的所有堆内存
 *
 * @param[in,out] t 追踪上下文指针
 */
void ai_trace_free(ai_trace_t* t);

/**
 * @brief 记录一次 Function/Tool 工具调用跨度 (Span)
 *
 * 将工具执行元数据（工具名、执行耗时毫秒、返回字节数、执行成功标志）以 JSON 格式追加至 tool_spans 数组中。
 *
 * @param[in,out] t 追踪上下文指针
 * @param[in] name 工具函数名（如 "get_assets", "query_transactions"）
 * @param[in] latency_ms 工具执行耗时（毫秒）
 * @param[in] bytes 工具返回的 JSON 结果字节数
 * @param[in] ok 执行状态（1: 成功，0: 失败）
 */
void ai_trace_add_tool_span(ai_trace_t* t, const char* name, long latency_ms, size_t bytes, int ok);
