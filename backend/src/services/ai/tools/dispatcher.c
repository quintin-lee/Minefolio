#include "services/ai/tools/dispatcher.h"
#include "services/ai/policy/policy.h"
#include "services/ai_tools.h"
#include <stdlib.h>
#include <string.h>

char*
ai_tool_dispatch_parsed(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        const char*      name,
                        csilk_json_t*    args)
{
    if (!name) {
        return strdup("{\"error\":\"tool name is required\"}");
    }

    ai_policy_decision_t* decision = ai_policy_evaluate(user_id, name, args);
    if (decision) {
        if (!decision->allowed) {
            char* err_res = decision->reason ? strdup(decision->reason)
                                             : strdup("{\"error\":\"operation denied by policy\"}");
            ai_policy_decision_free(decision);
            return err_res;
        }
        if (decision->requires_confirmation && decision->draft) {
            char* draft_json = ai_confirmation_draft_to_json(decision->draft);
            ai_policy_decision_free(decision);
            return draft_json;
        }
        ai_policy_decision_free(decision);
    }

    return ai_tools_execute_parsed(pool, user_id, args, name);
}

char*
ai_tool_dispatch(csilk_db_pool_t* pool, int64_t user_id, const char* name, const char* arguments)
{
    if (!name) {
        return strdup("{\"error\":\"tool name is required\"}");
    }
    csilk_json_t* args = NULL;
    if (arguments && arguments[0]) {
        args = csilk_json_parse(arguments);
    }
    char* res = ai_tool_dispatch_parsed(pool, user_id, name, args);
    if (args) {
        csilk_json_free(args);
    }
    return res;
}
