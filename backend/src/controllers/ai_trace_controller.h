/**
 * @file ai_trace_controller.h
 * @brief AI 链路追踪（Trace）、Token 消耗与工具调用 Span 监控控制器头文件
 *
 * 声明 AI 交互链路追踪记录的分页检索、Token 消耗/响应耗时统计指标
 * 以及单次对话详情（包含 Prompt、Response、工具调用跨度 Tool Spans）相关的 HTTP 路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 注册 AI 链路追踪与指标统计相关的所有 HTTP 路由
 *
 * @details 注册包括追踪列表、聚合统计与链路详情端点：
 *          - GET /api/ai/traces: 分页获取 AI 交互链路追踪列表
 *          - GET /api/ai/traces/stats: 获取 Token 消耗、平均耗时与调用量统计
 *          - GET /api/ai/traces/:id: 获取单次 AI 交互的完整 Trace 详情
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_ai_trace_routes(csilk_app_t* app);
