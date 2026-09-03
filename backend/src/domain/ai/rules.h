#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief 校验并规范化会话标题
 */
int mf_ai_rule_validate_session_title(const char* title, char* out_title, size_t out_cap,
                                     char* err_buf, size_t err_len);

/**
 * @brief 约束上下文窗口轮数在合法安全区间 [5, 100]
 */
int mf_ai_rule_clamp_context_size(int requested_size);

/**
 * @brief 根据每百万 Token 单价计算模型推理成本 (美元/元)
 */
int mf_ai_rule_calculate_token_cost(int64_t prompt_tokens, int64_t completion_tokens,
                                   double prompt_unit_price, double completion_unit_price,
                                   double* out_cost);

/**
 * @brief 校验 AI 供应商标识符是否合法安全
 */
bool mf_ai_rule_is_safe_provider(const char* provider_id);
