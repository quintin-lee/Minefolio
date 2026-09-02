#pragma once
#include "csilk/csilk.h"
#include "common/ai_config.h"

/**
 * @file ai_service.h
 * @brief AI 大模型智能财务分析、流式对话、Function Calling 工具调用及工作流执行引擎
 */

/**
 * @brief 初始化 AI 引擎核心、加载系统/用户配置并注册默认 Function Calling 工具集
 * @param pool 数据库连接池
 */
void ai_init(csilk_db_pool_t* pool);

/**
 * @brief 释放并优雅关闭 AI 引擎资源
 */
void ai_shutdown(void);

/**
 * @brief 处理 AI 财务助理对话请求 (POST /api/ai/chat)
 * 支持 SSE (Server-Sent Events) 流式响应与多轮 Function Calling 自动执行
 * @param c HTTP 上下文
 */
void ai_chat_handler(csilk_ctx_t* c);

/**
 * @brief 测试大模型服务商 API Key 及端点连通性 (POST /api/ai/test-connection)
 * @param c HTTP 上下文
 */
void ai_service_test_connection(csilk_ctx_t* c);

/**
 * @brief 动态拉取指定服务商可用的模型列表 (POST /api/ai/fetch-models)
 * @param c HTTP 上下文
 */
void ai_service_fetch_models(csilk_ctx_t* c);

/**
 * @brief 获取全局 AI 运行时配置单例
 * @return ai_config_t* 配置对象指针
 */
ai_config_t* ai_get_config(void);

/**
 * @brief 执行深度财务诊断工作流并以 SSE 流式推送诊断报告
 *
 * @param c                    HTTP 上下文
 * @param user_id              用户 ID
 * @param session_id           对话会话 ID
 * @param workflow_title       工作流标题（如 "资产配置与流动性分析"）
 * @param structured_data_json 提取的结构化财务数据 JSON 字符串
 * @return int                 0 成功启动流式推送；-1 出错
 */
int ai_service_stream_report(csilk_ctx_t* c,
                             int64_t      user_id,
                             int64_t      session_id,
                             const char*  workflow_title,
                             const char*  structured_data_json);
