#include <assert.h>
#include <stdio.h>
#include "services/ai/policy/policy.h"
#include "services/ai/policy/risk.h"

int
main(void)
{
    /* Pin deterministic rules: confirmation enforced for medium+, no freq cap in the way. */
    ai_policy_rules_t rules = {
        .single_amount_limit = 500000.0,
        .large_amount_threshold = 50000.0,
        .max_frequency_per_minute = 60,
        .enforce_confirmation = true,
    };
    ai_policy_set_rules(&rules);
    ai_policy_reset_frequency_limits();

    /* A real user id so auth passes; tool name looks like a read tool by prefix
     * so the name-pattern fallback would compute a low/readonly risk. */
    const char* tool = "mcp:7:get_foo";

    /* No override -> today's name-pattern behavior (low-ish, not CRITICAL). */
    ai_policy_decision_t* d1 = ai_policy_evaluate(1, 0, tool, NULL, AI_RISK_NO_OVERRIDE);
    assert(d1);
    assert(d1->risk_level < AI_RISK_CRITICAL);
    ai_policy_decision_free(d1);

    /* Override with HIGH -> the decision must carry HIGH and require confirmation
     * (enforce_confirmation is pinned true above). */
    ai_policy_decision_t* d2 = ai_policy_evaluate(1, 0, tool, NULL, AI_RISK_HIGH);
    assert(d2);
    assert(d2->risk_level == AI_RISK_HIGH);
    assert(d2->requires_confirmation == true);
    ai_policy_decision_free(d2);

    printf("PASS: test_policy_mcp_risk\n");
    return 0;
}
