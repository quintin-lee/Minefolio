#pragma once
#include "services/ai/policy/permission.h"
#include "services/ai/policy/risk.h"
#include "services/ai/policy/confirmation.h"
#include "services/ai/policy/audit.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double single_amount_limit;      /**< 单笔最高金额上限（超过则硬拦截，默认 500,000） */
    double large_amount_threshold;   /**< 大额临界值（超过则自动升至 CRITICAL 风险，默认 50,000） */
    int    max_frequency_per_minute; /**< 每分钟最高调用频次限制（默认 60 次/分） */
    bool   enforce_confirmation;     /**< 是否强制所有变更操作需要二次确认 */
} ai_policy_rules_t;

typedef struct {
    bool                     allowed;               /**< 策略是否放行 */
    bool                     requires_confirmation; /**< 是否需要确认 */
    ai_risk_level_t          risk_level;            /**< 综合评估风险等级 */
    ai_permission_level_t    perm_level;            /**< 工具所需权限 */
    ai_confirmation_draft_t* draft;                 /**< 若需确认，生成对应的草案对象 */
    char                     reason[256];           /**< 放行或拦截的详细原因 */
} ai_policy_decision_t;

/**
 * @brief 获取全局默认策略规则
 */
ai_policy_rules_t ai_policy_get_default_rules(void);

/**
 * @brief 设置自定义全局策略规则（用于运行时微调或测试）
 */
void ai_policy_set_rules(const ai_policy_rules_t* rules);

/**
 * @brief 重置用户频次计数器（主要用于单元测试）
 */
void ai_policy_reset_frequency_limits(void);

/**
 * @brief 执行完整的安全策略综合评估 (Authentication → Authorization → Policy → Risk → Confirmation)
 *
 * 检查项包括：
 * 1. 认证状态 (user_id > 0)
 * 2. 授权权限 (user_role vs tool permission)
 * 3. 频次限流 (frequency rate limit)
 * 4. 金额风控 (amount limit & dynamic risk escalation)
 * 5. 二次确认要求 (confirmation requirement)
 *
 * @param user_id 用户 ID
 * @param session_id 会话 ID
 * @param tool_name 目标工具名称
 * @param args 参数对象
 * @return ai_policy_decision_t* 策略决策对象（调用方通过 ai_policy_decision_free 释放）
 */
ai_policy_decision_t* ai_policy_evaluate(int64_t             user_id,
                                         int64_t             session_id,
                                         const char*         tool_name,
                                         const csilk_json_t* args);

/**
 * @brief 释放策略决策对象
 */
void ai_policy_decision_free(ai_policy_decision_t* decision);

#ifdef __cplusplus
}
#endif
