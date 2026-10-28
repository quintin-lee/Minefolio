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
 * @brief 解析供应商/模型的有效每百万 Token 价格（USD）
 *
 * 无配置覆盖时按默认价表回落（与既有 trace 估算口径一致）：
 * gpt-4o 2.50/10.00，gpt-4o-mini 0.15/0.60，deepseek 0.14/0.28，
 * qwen 0.20/0.60，ollama 0/0，其余 0.50/1.50。
 * 配置价（provider_in / provider_out，均为「每百万 token 美元」单价指针）逐侧覆盖
 * 默认表：值 >0 时该侧采用配置价，<=0 或指针为 NULL 时该侧保持默认表值；
 * 因此可以只配一侧。任一传入值为负视为非法。
 *
 * @param provider 供应商标识（可 NULL）
 * @param model 模型名（可 NULL）
 * @param provider_in ai.json 配置输入价指针（可 NULL）
 * @param provider_out ai.json 配置输出价指针（可 NULL）
 * @param out_in [out] 生效输入价（USD/百万 token）
 * @param out_out [out] 生效输出价
 * @return 0 成功；-1 参数非法（out 为 NULL，或配置价为负）
 */
int mf_ai_rule_model_prices(const char*   provider,
                            const char*   model,
                            const double* provider_in,
                            const double* provider_out,
                            double*       out_in,
                            double*       out_out);

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
