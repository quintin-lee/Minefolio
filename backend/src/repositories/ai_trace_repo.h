#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include "common/ai_trace.h"

/**
 * @file ai_trace_repo.h
 * @brief AI 调用链路追踪与可观测性数据访问层接口
 *
 * 提供 AI 请求的完整生命周期可观测性数据持久化与检索支持，包括 Token 消耗、
 * 请求耗时、首字延迟 (TTFT)、每秒 Token 生成速度、美元成本以及工具调用 (Tool Spans) 链路追踪。
 */

/**
 * @brief 分页及按服务商/模型筛选查询 AI 调用追踪记录列表
 *
 * 动态拼接 WHERE 条件（支持 provider 和 model 过滤），两阶段执行：
 * 1. 统计符合条件的总 Trace 记录数。
 * 2. 分页按创建时间倒序 (`created_at DESC, id DESC`) 查询 Trace 概览列表。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param page 当前页码 (从 1 开始)
 * @param page_size 每页记录数
 * @param provider 可选过滤服务商 (如 "openai", "deepseek")，为 NULL 或空时不限制
 * @param model 可选过滤模型名称 (如 "gpt-4o", "deepseek-chat")，为 NULL 或空时不限制
 * @param[out] total 输出参数，返回符合筛选条件的总记录数指针
 * @return csilk_json_t* 包含 Trace 概览对象的 JSON 数组
 */
csilk_json_t* ai_trace_list(csilk_db_pool_t* pool,
                            int64_t          user_id,
                            int64_t          page,
                            int64_t          page_size,
                            const char*      provider,
                            const char*      model,
                            int64_t*         total);

/**
 * @brief 根据 Trace ID 查询单条 AI 调用的完整追踪详情
 *
 * 检索完整的提示词 (input_messages)、生成内容 (output_content)、系统提示词 (system_prompt)、
 * 元数据 (metadata 包含 tool_spans) 等详细数据。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（隔离校验）
 * @param id Trace 记录 ID
 * @return csilk_json_t* 包含单条 Trace 详情对象的 JSON 数组（长度为 1）；未命中返回 NULL
 */
csilk_json_t* ai_trace_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 聚合统计指定用户的 AI 调用总体指标
 *
 * 聚合计算指标包括：总调用次数 (total_traces)、总消耗 Token (total_tokens)、
 * 平均总耗时 (avg_latency_ms)、平均首字耗时 (avg_first_token_ms)、
 * 平均吞吐速度 (avg_tokens_per_sec)、总估算美元成本 (total_cost_usd)。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 包含统计指标单行结果的 JSON 数组
 */
csilk_json_t* ai_trace_stats(csilk_db_pool_t* pool, int64_t user_id);
