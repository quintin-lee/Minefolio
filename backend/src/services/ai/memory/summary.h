#pragma once
#include "csilk/csilk.h"
#include "services/ai/runtime/context.h"
#include <stdint.h>

/**
 * @brief 检查 token 使用是否达到预算 80% 阈值，满足条件则异步生成并持久化会话摘要。
 *
 * 在 loop.c 的 Agent 循环中每轮结束后调用。若 `ctx->stats.total_tokens >=
 * ctx->limits.token_budget * 0.8`，则 spawn 一个 detached 线程：
 * 1. 从 DB 读取该 session 最近 N 条消息 + 已有摘要
 * 2. 调用 LLM 生成新摘要
 * 3. 通过 `mf_ai_summary_upsert` 写回 `ai_session_summaries` 表
 *
 * 线程池内部有互斥锁 + 每 session 防重入标志，避免高频重复触发。
 *
 * @param pool 数据库池（可为 NULL，此时跳过）
 * @param ctx  AI 运行时上下文（提供 user_id, session_id, limits, stats, provider_id, model_name）
 */
void ai_summary_maybe_trigger(csilk_db_pool_t* pool, ai_runtime_context_t* ctx);
