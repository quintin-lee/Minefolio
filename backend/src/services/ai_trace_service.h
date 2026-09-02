#pragma once
#include "csilk/csilk.h"

/**
 * @file ai_trace_service.h
 * @brief AI 大模型调用全链路可观测性 Tracing 审计追踪与 Token 消耗统计服务
 */

/**
 * @brief 分页查询 AI 调用 Trace 链路列表 (GET /api/ai/traces)
 * 支持按 Provider (openai, deepseek, ollama)、Model、执行状态 (success/error)、时间范围过滤
 * @param c HTTP 上下文
 */
void ai_trace_service_list(csilk_ctx_t* c);

/**
 * @brief 查询单个 Trace 的完整执行链路详情 (GET /api/ai/traces/:id)
 * 包含 Prompt 输入消息、Completion 输出、Tool Calls 跨度 (Tool Spans) 耗时及元数据
 * @param c HTTP 上下文
 */
void ai_trace_service_get(csilk_ctx_t* c);

/**
 * @brief 获取 AI 调用的汇总统计大盘 (GET /api/ai/traces/stats)
 * 返回总调用次数、成功率、Prompt/Completion Tokens 累计消耗与平均时延
 * @param c HTTP 上下文
 */
void ai_trace_service_stats(csilk_ctx_t* c);
