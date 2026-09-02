#pragma once
#include "services/ai/policy/permission.h"
#include "services/ai/policy/risk.h"
#include "services/ai/policy/confirmation.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool                     allowed;
    bool                     requires_confirmation;
    ai_risk_level_t          risk_level;
    ai_permission_level_t    perm_level;
    ai_confirmation_draft_t* draft;
    char*                    reason;
} ai_policy_decision_t;

/**
 * @brief 执行统一安全策略检查
 * @param user_id 用户 ID
 * @param tool_name 工具或操作名称
 * @param args 解析后的参数 JSON 对象
 * @return 策略决议对象（调用方需调用 ai_policy_decision_free 释放）
 */
ai_policy_decision_t*
ai_policy_evaluate(int64_t user_id, const char* tool_name, const csilk_json_t* args);

/**
 * @brief 释放策略决议对象
 */
void ai_policy_decision_free(ai_policy_decision_t* decision);

#ifdef __cplusplus
}
#endif
