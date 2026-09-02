/**
 * @file ai_controller.h
 * @brief AI 智能助手、对话会话管理、配置及财务工作流控制器头文件
 *
 * 声明 AI 模型发现、SSE 流式对话、会话增删改查、历史消息列表、
 * AI 供应商配置管理（支持密钥端到端加解密）以及预设财务分析工作流（Workflows）相关的 HTTP 路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 注册 AI 助手、会话、配置及工作流相关的所有 HTTP 路由
 *
 * @details 注册包括模型列表、SSE 流式对话、会话管理、消息历史、供应商配置与智能分析工作流等端点：
 *          - GET    /api/ai/models: 获取可用模型列表
 *          - POST   /api/ai/chat: SSE 流式 AI 对话
 *          - GET    /api/ai/sessions: 获取会话列表
 *          - POST   /api/ai/sessions: 创建新会话
 *          - GET    /api/ai/sessions/:id: 获取会话详情
 *          - PUT    /api/ai/sessions/:id: 更新会话元数据
 *          - DELETE /api/ai/sessions/:id: 删除会话
 *          - GET    /api/ai/sessions/:id/messages: 获取会话历史消息
 *          - GET    /api/settings/ai: 获取 AI 供应商配置
 *          - PUT    /api/settings/ai: 保存并热重载 AI 配置
 *          - POST   /api/settings/ai/test: 测试供应商连通性
 *          - POST   /api/settings/ai/fetch-models: 动态拉取供应商模型列表
 *          - GET    /api/ai/workflows: 获取内置财务分析工作流定义
 *          - POST   /api/ai/workflows/run: SSE 流式执行财务分析工作流
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_ai_routes(csilk_app_t* app);
