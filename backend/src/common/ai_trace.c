/**
 * @file ai_trace.c
 * @brief AI 会话调用全链路追踪 (Trace)、性能指标与成本审计实现
 *
 * 实现了流式文本聚合与 Token 统计、中英文混合 Token 启发式估算、
 * 单调时钟耗时统计 (Latency & TTFT)、Tool 调用 Span 记录与堆资源释放。
 */

#include "common/ai_trace.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief 初始化 AI 追踪上下文
 *
 * @param[out] t 追踪结构体指针
 * @param[in] user_id 用户 ID
 * @param[in] session_id 会话 ID
 */
void
ai_trace_init(ai_trace_t* t, int64_t user_id, int64_t session_id)
{
    memset(t, 0, sizeof(*t));
    t->user_id = user_id;
    t->session_id = session_id;
    t->input_messages = strdup("[]");
    t->output_content = strdup("");
    t->system_prompt = strdup("");
    t->error_message = strdup("");
    t->metadata = strdup("{}");
    strncpy(t->tool_spans, "[]", sizeof(t->tool_spans) - 1);
    t->tool_spans[sizeof(t->tool_spans) - 1] = '\0';
    strncpy(t->status, "ok", sizeof(t->status) - 1);
    clock_gettime(CLOCK_MONOTONIC, &t->t_start);
}

/**
 * @brief 记录单次工具调用跨度 (Span)
 *
 * @param[in,out] t 追踪上下文
 * @param[in] name 工具名称
 * @param[in] latency_ms 执行耗时（毫秒）
 * @param[in] bytes 返回结果字节数
 * @param[in] ok 执行状态（1 成功，0 失败）
 */
void
ai_trace_add_tool_span(ai_trace_t* t, const char* name, long latency_ms, size_t bytes, int ok)
{
    if (!t || !name) {
        return;
    }
    /* 限制固定缓冲区长度以防溢出 */
    size_t cur = strlen(t->tool_spans);
    if (cur + 256 >= sizeof(t->tool_spans)) {
        return;
    }
    char entry[256];
    int  n = snprintf(entry,
                      sizeof(entry),
                      "{\"name\":\"%s\",\"latency_ms\":%ld,\"bytes\":%zu,\"ok\":%d}",
                      name,
                      latency_ms,
                      bytes,
                      ok ? 1 : 0);
    if (n < 0) {
        return;
    }
    const char* sep = (cur <= 2) ? "" : ","; /* 首个元素无需前置逗号 */
    /* 替换末尾的 ']' 为 ',entry]' */
    size_t pos = cur > 0 ? cur - 1 : 0;
    if (t->tool_spans[pos] == ']') {
        t->tool_spans[pos] = '\0';
    }
    int written = snprintf(t->tool_spans + strlen(t->tool_spans),
                           sizeof(t->tool_spans) - strlen(t->tool_spans),
                           "%s%s]",
                           sep,
                           entry);
    (void)written;
}

/**
 * @brief 设置服务商与模型
 *
 * @param[in,out] t 追踪结构体
 * @param[in] provider 提供商
 * @param[in] model 模型名称
 */
void
ai_trace_set_provider(ai_trace_t* t, const char* provider, const char* model)
{
    strncpy(t->provider, provider ?: "", sizeof(t->provider) - 1);
    strncpy(t->model, model ?: "", sizeof(t->model) - 1);
}

/**
 * @brief 设置模型超参数
 *
 * @param[in,out] t 追踪结构体
 * @param[in] temperature 温度
 * @param[in] max_tokens 最大 Token
 * @param[in] top_p 核采样
 */
void
ai_trace_set_params(ai_trace_t* t, double temperature, int max_tokens, double top_p)
{
    t->temperature = temperature;
    t->max_tokens = max_tokens;
    t->top_p = top_p;
}

/**
 * @brief 设置系统提示词
 *
 * @param[in,out] t 追踪结构体
 * @param[in] prompt 提示词文本
 */
void
ai_trace_set_system_prompt(ai_trace_t* t, const char* prompt)
{
    free(t->system_prompt);
    t->system_prompt = strdup(prompt ?: "");
}

/**
 * @brief 序列化消息列表并存储
 *
 * @param[in,out] t 追踪结构体
 * @param[in] messages_array 消息 JSON 数组
 */
void
ai_trace_serialize_messages(ai_trace_t* t, csilk_json_t* messages_array)
{
    free(t->input_messages);
    size_t slen = 0;
    char*  s = csilk_json_serialize(messages_array, &slen);
    t->input_messages = s ? s : strdup("[]");
}

/**
 * @brief 追加流式输出内容
 *
 * @param[in,out] t 追踪结构体
 * @param[in] chunk 输出片段
 */
void
ai_trace_append_output(ai_trace_t* t, const char* chunk)
{
    if (!chunk || !chunk[0]) {
        return;
    }
    size_t clen = strlen(chunk);
    size_t olen = strlen(t->output_content);
    size_t nlen = olen + clen + 1;
    char*  buf = (char*)realloc(t->output_content, nlen);
    if (!buf) {
        return;
    }
    memcpy(buf + olen, chunk, clen);
    buf[olen + clen] = '\0';
    t->output_content = buf;
    t->accumulated_len = (long)nlen;
}

/**
 * @brief 记录首字响应时刻 (TTFT)
 *
 * @param[in,out] t 追踪结构体
 */
void
ai_trace_record_first_token(ai_trace_t* t)
{
    if (!t->has_first_token) {
        clock_gettime(CLOCK_MONOTONIC, &t->t_first_token);
        t->has_first_token = 1;
        t->first_token_ms = (t->t_first_token.tv_sec - t->t_start.tv_sec) * 1000 +
                            (t->t_first_token.tv_nsec - t->t_start.tv_nsec) / 1000000;
    }
}

/**
 * @brief 启发式估算文本所占用的 Token 数量
 *
 * @param[in] text 待估算文本
 * @return int 预估 Token 数
 */
int
ai_estimate_tokens_from_text(const char* text)
{
    if (!text || !*text) {
        return 0;
    }
    size_t char_count = 0;
    size_t ascii_count = 0;
    size_t cjk_count = 0;
    size_t i = 0;

    while (text[i] != '\0') {
        unsigned char c = (unsigned char)text[i];
        if ((c & 0x80) == 0) {
            ascii_count++;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            cjk_count++;
            i += (text[i + 1] != '\0') ? 2 : 1;
        } else if ((c & 0xF0) == 0xE0) {
            cjk_count++;
            i += (text[i + 1] != '\0' && text[i + 2] != '\0') ? 3 : 1;
        } else if ((c & 0xF8) == 0xF0) {
            cjk_count++;
            i += (text[i + 1] != '\0' && text[i + 2] != '\0' && text[i + 3] != '\0') ? 4 : 1;
        } else {
            ascii_count++;
            i += 1;
        }
        char_count++;
    }

    /* 标准启发式：~3.5 个 ASCII 字符折合 1 个 Token，每个 CJK 字符约 1.2 个 Token */
    int estimated = (int)((double)ascii_count / 3.5 + (double)cjk_count * 1.2);
    if (estimated <= 0 && char_count > 0) {
        estimated = 1;
    }
    return estimated;
}

/**
 * @brief 计算 Token 与花费金额
 *
 * @param[in,out] t 追踪结构体
 * @param[in] prompt_tokens 输入 Token
 * @param[in] completion_tokens 输出 Token
 */
void
ai_trace_calculate_tokens_and_cost(ai_trace_t* t, int prompt_tokens, int completion_tokens)
{
    if (prompt_tokens > 0) {
        t->prompt_tokens = prompt_tokens;
    } else {
        int est_in = ai_estimate_tokens_from_text(t->input_messages);
        int est_sys = ai_estimate_tokens_from_text(t->system_prompt);
        t->prompt_tokens = est_in + est_sys + 8; /* 附加对话消息包装开销 */
    }

    if (completion_tokens > 0) {
        t->completion_tokens = completion_tokens;
    } else {
        t->completion_tokens = ai_estimate_tokens_from_text(t->output_content);
    }

    t->total_tokens = t->prompt_tokens + t->completion_tokens;

    /* 价格估算（每 100 万 Token 美元单价） */
    double in_price_per_1m = 0.50;
    double out_price_per_1m = 1.50;

    if (strstr(t->model, "gpt-4o-mini") || strstr(t->model, "4o-mini")) {
        in_price_per_1m = 0.15;
        out_price_per_1m = 0.60;
    } else if (strstr(t->model, "gpt-4o") || strstr(t->model, "gpt-4")) {
        in_price_per_1m = 2.50;
        out_price_per_1m = 10.00;
    } else if (strstr(t->provider, "deepseek") || strstr(t->model, "deepseek")) {
        in_price_per_1m = 0.14;
        out_price_per_1m = 0.28;
    } else if (strstr(t->provider, "qwen") || strstr(t->model, "qwen")) {
        in_price_per_1m = 0.20;
        out_price_per_1m = 0.60;
    } else if (strstr(t->provider, "ollama")) {
        in_price_per_1m = 0.0;
        out_price_per_1m = 0.0;
    }

    t->cost_usd = ((double)t->prompt_tokens * in_price_per_1m / 1000000.0) +
                  ((double)t->completion_tokens * out_price_per_1m / 1000000.0);
}

/**
 * @brief 结束追踪并计算总延迟与生成速率
 *
 * @param[in,out] t 追踪结构体
 * @param[in] status 状态 ("ok"/"error")
 * @param[in] error 错误描述
 */
void
ai_trace_finish(ai_trace_t* t, const char* status, const char* error)
{
    clock_gettime(CLOCK_MONOTONIC, &t->t_end);
    strncpy(t->status, status ?: "ok", sizeof(t->status) - 1);
    if (error && error[0]) {
        free(t->error_message);
        t->error_message = strdup(error);
    }
    t->latency_ms = (t->t_end.tv_sec - t->t_start.tv_sec) * 1000 +
                    (t->t_end.tv_nsec - t->t_start.tv_nsec) / 1000000;
    if (t->latency_ms < 0) {
        t->latency_ms = 0;
    }

    /* 计算生成速率 tokens/s */
    if (t->completion_tokens > 0) {
        long gen_ms = (t->first_token_ms > 0 && t->latency_ms > t->first_token_ms)
                          ? (t->latency_ms - t->first_token_ms)
                          : t->latency_ms;
        if (gen_ms > 0) {
            t->tokens_per_sec = (double)t->completion_tokens / ((double)gen_ms / 1000.0);
        } else {
            t->tokens_per_sec = (double)t->completion_tokens;
        }
    } else if (t->total_tokens > 0 && t->latency_ms > 0) {
        t->tokens_per_sec = (double)t->total_tokens / ((double)t->latency_ms / 1000.0);
    }
}

/**
 * @brief 释放追踪对象内部堆内存
 *
 * @param[in,out] t 追踪结构体
 */
void
ai_trace_free(ai_trace_t* t)
{
    if (!t) {
        return;
    }
    free(t->input_messages);
    free(t->output_content);
    free(t->system_prompt);
    free(t->error_message);
    free(t->metadata);
    memset(t, 0, sizeof(*t));
}
