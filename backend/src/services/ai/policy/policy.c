#include "services/ai/policy/policy.h"
#include <stdlib.h>
#include <string.h>

ai_policy_decision_t*
ai_policy_evaluate(int64_t user_id, const char* tool_name, const csilk_json_t* args)
{
    ai_policy_decision_t* dec = (ai_policy_decision_t*)calloc(1, sizeof(ai_policy_decision_t));
    if (!dec) {
        return NULL;
    }

    dec->perm_level = ai_permission_get_level(tool_name);
    dec->risk_level = ai_risk_assess(tool_name, args);

    if (!ai_permission_check(user_id, dec->perm_level)) {
        dec->allowed = false;
        dec->reason = strdup("Permission denied");
        return dec;
    }

    dec->allowed = true;

    if (dec->risk_level >= AI_RISK_HIGH) {
        dec->requires_confirmation = true;
        size_t len = 0;
        char*  raw_args = args ? csilk_json_serialize(args, &len) : strdup("{}");
        dec->draft = ai_confirmation_draft_create(
            tool_name, "高风险财务操作需二次确认", raw_args, "操作金额或风险较大，请谨慎核对");
        if (raw_args) {
            free(raw_args);
        }
    }

    return dec;
}

void
ai_policy_decision_free(ai_policy_decision_t* decision)
{
    if (!decision) {
        return;
    }
    if (decision->reason) {
        free(decision->reason);
    }
    if (decision->draft) {
        ai_confirmation_draft_free(decision->draft);
    }
    free(decision);
}
