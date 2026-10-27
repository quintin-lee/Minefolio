#pragma once
#include "csilk/csilk.h"
#include "services/ai/runtime/context.h"
#include <stdint.h>

/**
 * @brief 检查 token 使用是否达到预算 80% 阈值，满足条件则异步生成会话摘要。
 *
 * 在 loop.c 的 Agent 循环中每轮结束后调用。若 `ctx->stats.total_tokens >=
 * ctx->limits.token_budget * 0.8`，则 spawn 一个 detached 线程：
 * 1. 在当前（主）线程读取该 session 最近 N 条消息 + 已有摘要并构建 prompt
 * 2. worker 线程只调用 LLM 生成新摘要，结果暂存进程内槽位（不触库）
 * 3. 由主线程在下次交互（ai_summary_flush_pending）时写回 `ai_session_summaries`
 *
 * 之所以让 worker 完全脱离 DB：与请求线程共享的 csilk 单连接 SQLite 池
 * 无法跨线程安全读写（无线程安全的连接池）。
 * 状态表内部有互斥锁 + 每 session 防重入标志，避免高频重复触发；
 * 本函数会先 flush 该 session 的暂存摘要。
 *
 * @param pool 数据库池（可为 NULL，此时跳过）
 * @param ctx  AI 运行时上下文（提供 user_id, session_id, limits, stats, provider_id, model_name）
 */
void ai_summary_maybe_trigger(csilk_db_pool_t* pool, ai_runtime_context_t* ctx);

/**
 * @brief 把该会话暂存的异步摘要串行落库（主线程调用）
 *
 * 应在每次读取会话摘要（注入上下文）或触发新摘要之前调用，保证摘要的
 * 「生成 → 持久化」链路始终由请求线程完成、不与 worker 线程共享连接。
 *
 * @param pool 数据库池（NULL 时跳过）
 * @param session_id 会话 ID（<=0 时跳过）
 */
void ai_summary_flush_pending(csilk_db_pool_t* pool, int64_t session_id);
