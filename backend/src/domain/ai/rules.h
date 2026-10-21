#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief 校验并规范化会话标题
 */
int mf_ai_rule_validate_session_title(
    const char* title, char* out_title, size_t out_cap, char* err_buf, size_t err_len);

/**
 * @brief 约束上下文窗口轮数在合法安全区间 [5, 100]
 */
int mf_ai_rule_clamp_context_size(int requested_size);

/**
 * @brief 根据每百万 Token 单价计算模型推理成本 (美元/元)
 */
int mf_ai_rule_calculate_token_cost(int64_t prompt_tokens,
                                    int64_t completion_tokens,
                                    double  prompt_unit_price,
                                    double  completion_unit_price,
                                    double* out_cost);

/**
 * @brief 校验 AI 供应商标识符是否合法安全
 */
bool mf_ai_rule_is_safe_provider(const char* provider_id);

/**
 * @brief 校验 AI 抓取目标的 URL 合法性与 SSRF 安全性
 * 规则：必须为 http:// 或 https:// 开头；长度不超过 max_len（默认上限 4096）；
 *       host 不得为内网/回环地址（localhost/127./10./172.16-31./192.168./169.254./::1/0.0.0.0）。
 */
bool mf_ai_rule_validate_url(const char* url, size_t max_len);
