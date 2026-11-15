#pragma once
#include "services/ai/tools/registry.h"
#include "services/ai/tools/schema.h"

/**
 * @brief 注册市场脉搏 AI 工具
 *
 * 工具：
 *   - get_market_pulse: 读取用户最近 N 天的 AI 每日市场脉搏分析报告
 *     （含市场概要、持仓影响、操作建议、风险提示）。LLM 在对话中可据此
 *     回答「今天的市场对我的持仓有什么影响」。
 */
void ai_tool_news_register_all(void);
