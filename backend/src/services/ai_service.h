#pragma once
#include "csilk/csilk.h"
#include "common/ai_config.h"
#include <stddef.h>

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

#include "application/ai/config_provider.h"

/**
 * @brief 获取当前全局 AI 运行时配置（内部调用 ai_config_acquire）。
 *        调用者须在用完配置（及由此派生的 ai_provider_t* 指针）后
 *        调用 ai_config_release() 归还引用，否则快照引用泄漏。
 *        为历史 API 保留；新代码请直接用 ai_config_acquire()/ai_config_release() 对。
 * @return ai_config_t* 配置对象指针，未初始化时返回 NULL
 */
ai_config_t* ai_get_config(void);

/**
 * @brief 获取当前快照预建的 csilk_ai_t 实例（需先 ai_config_acquire）。
 *        返回的实例随快照生命周期，acquire 持有期间保证有效。
 * @return csilk_ai_t* 实例指针或 NULL（未配置/实例创建失败）
 */
static inline csilk_ai_t*
ai_snapshot_ai_instance(const ai_config_t* cfg)
{
    return ai_config_get_ai_instance(cfg);
}

/**
 * @brief 执行深度财务诊断工作流并以 SSE 流式推送诊断报告
 *
 * @param c                    HTTP 上下文
 * @param user_id              用户 ID
 * @param session_id           对话会话 ID
 * @param workflow_title       工作流标题（如 "资产配置与流动性分析"）
 * @param structured_data_json 提取的结构化财务数据 JSON 字符串
 * @param user_prompt          用户发起工作流的展示提示（落库 ai_messages user 行；可 NULL）
 * @return int                 0 成功启动流式推送；-1 出错
 */
int ai_service_stream_report(csilk_ctx_t* c,
                             int64_t      user_id,
                             int64_t      session_id,
                             const char*  workflow_title,
                             const char*  structured_data_json,
                             const char*  user_prompt);
