#include "services/ai/policy/risk.h"
#include "common/db.h"
#include <string.h>

ai_risk_level_t
ai_risk_assess(const char* tool_name, const csilk_json_t* args)
{
    if (!tool_name) {
        return AI_RISK_LOW;
    }
    if (strcmp(tool_name, "delete_asset") == 0 || strcmp(tool_name, "reset_account") == 0) {
        return AI_RISK_CRITICAL;
    }

    if (args) {
        double amount = db_get_num(args, "amount");
        if (amount >= 50000.0) {
            return AI_RISK_HIGH;
        }
        if (amount >= 10000.0) {
            return AI_RISK_MEDIUM;
        }
    }

    if (strcmp(tool_name, "create_transaction") == 0 || strcmp(tool_name, "update_asset") == 0) {
        return AI_RISK_MEDIUM;
    }

    return AI_RISK_LOW;
}
