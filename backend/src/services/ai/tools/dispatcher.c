#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/validation.h"
#include "services/ai/policy/policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char*
ai_tool_dispatch_parsed(const ai_tool_context_t* ctx,
                        const char*              tool_name,
                        const csilk_json_t*      args)
{
    if (!ctx || !tool_name || !tool_name[0]) {
        return strdup("{\"error\":\"invalid tool context or tool name\"}");
    }

    /* 1. Tool Resolver */
    const ai_tool_t* tool = ai_tool_find(tool_name);
    if (!tool) {
        return strdup("{\"error\":\"unknown tool\"}");
    }

    /* 2. Schema Validator */
    char err_buf[256] = {0};
    if (ai_tool_validate_args(tool->parameters_schema, args, err_buf, sizeof(err_buf)) != 0) {
        csilk_json_t* err_obj = csilk_json_object();
        csilk_json_add_string(err_obj, "error", err_buf[0] ? err_buf : "schema validation failed");
        size_t len = 0;
        char*  err_str = csilk_json_serialize(err_obj, &len);
        csilk_json_free(err_obj);
        return err_str ? err_str : strdup("{\"error\":\"schema validation failed\"}");
    }

    if (tool->validate) {
        if (tool->validate(tool, args, err_buf, sizeof(err_buf)) != 0) {
            csilk_json_t* err_obj = csilk_json_object();
            csilk_json_add_string(
                err_obj, "error", err_buf[0] ? err_buf : "business validation failed");
            size_t len = 0;
            char*  err_str = csilk_json_serialize(err_obj, &len);
            csilk_json_free(err_obj);
            return err_str ? err_str : strdup("{\"error\":\"business validation failed\"}");
        }
    }

    /* 3. Permission Check */
    if (!ai_permission_check(ctx->user_id, tool->permission)) {
        return strdup("{\"error\":\"unauthorized user or insufficient permissions\"}");
    }

    /* 4. Risk Check */
    ai_risk_level_t risk = tool->risk;
    if (risk == AI_RISK_LOW) {
        risk = ai_risk_assess(tool_name, args);
    }

    /* 5. Normalize Args */
    csilk_json_t* normalized = ai_tool_normalize_args(tool->parameters_schema, args);

    /* 6. Executor */
    char* result = NULL;
    if (tool->execute) {
        result = tool->execute(tool, ctx, normalized);
    } else {
        result = strdup("{\"error\":\"tool executor not implemented\"}");
    }

    if (normalized) {
        csilk_json_free(normalized);
    }

    return result ? result : strdup("{\"error\":\"empty tool execution result\"}");
}

char*
ai_tool_dispatch(const ai_tool_context_t* ctx,
                 const char*              tool_name,
                 const char*              raw_arguments_json)
{
    if (!tool_name || !raw_arguments_json) {
        return strdup("{\"error\":\"missing tool_name or raw_arguments_json\"}");
    }

    csilk_json_t* args = csilk_json_parse(raw_arguments_json);
    if (!args) {
        return strdup("{\"error\":\"invalid json arguments\"}");
    }

    char* result = ai_tool_dispatch_parsed(ctx, tool_name, args);
    csilk_json_free(args);
    return result;
}
