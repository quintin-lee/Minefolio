#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/validation.h"
#include "services/ai/policy/policy.h"
#include "services/ai/policy/audit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char*
ai_tool_dispatch_parsed(const ai_tool_context_t* ctx,
                        const char*              tool_name,
                        const csilk_json_t*      args)
{
    /* 计算请求参数哈希用于全流程审计 */
    char args_hash[65] = {0};
    ai_confirmation_hash_args(args, args_hash, sizeof(args_hash));

    /* 1. 认证检验 (Authentication) */
    if (!ctx || ctx->user_id <= 0) {
        ai_audit_record_t audit = {
            .stage = AI_AUDIT_STAGE_REJECTION,
            .actor_id = ctx ? ctx->user_id : 0,
            .session_id = ctx ? ctx->session_id : 0,
            .risk = AI_RISK_READ_ONLY,
            .timestamp = (int64_t)time(NULL),
            .success = false,
        };
        strncpy(audit.tool, tool_name ? tool_name : "unknown", sizeof(audit.tool) - 1);
        strncpy(audit.arguments_hash, args_hash, sizeof(audit.arguments_hash) - 1);
        strncpy(audit.error_message,
                "Authentication failed: invalid user context",
                sizeof(audit.error_message) - 1);
        strncpy(audit.result_summary,
                "Rejected unauthenticated execution request",
                sizeof(audit.result_summary) - 1);
        ai_audit_log(&audit);

        return strdup("{\"error\":\"unauthorized: unauthenticated user context\"}");
    }

    if (!tool_name || !tool_name[0]) {
        return strdup("{\"error\":\"missing tool name\"}");
    }

    /* 2. 工具寻址 (Tool Resolver) */
    const ai_tool_t* tool = ai_tool_find(tool_name);
    if (!tool) {
        ai_audit_record_t audit = {
            .stage = AI_AUDIT_STAGE_REJECTION,
            .actor_id = ctx->user_id,
            .session_id = ctx->session_id,
            .risk = AI_RISK_LOW,
            .timestamp = (int64_t)time(NULL),
            .success = false,
        };
        strncpy(audit.tool, tool_name, sizeof(audit.tool) - 1);
        strncpy(audit.arguments_hash, args_hash, sizeof(audit.arguments_hash) - 1);
        strncpy(audit.error_message, "Unknown tool", sizeof(audit.error_message) - 1);
        strncpy(audit.result_summary,
                "Tool not registered in registry",
                sizeof(audit.result_summary) - 1);
        ai_audit_log(&audit);

        return strdup("{\"error\":\"unknown tool\"}");
    }

    /* 3. 参数模式校验 (Schema Validator) */
    char err_buf[256] = {0};
    if (ai_tool_validate_args(tool->parameters_schema, args, err_buf, sizeof(err_buf)) != 0) {
        ai_audit_record_t audit = {
            .stage = AI_AUDIT_STAGE_REJECTION,
            .actor_id = ctx->user_id,
            .session_id = ctx->session_id,
            .risk = tool->risk,
            .timestamp = (int64_t)time(NULL),
            .success = false,
        };
        strncpy(audit.tool, tool_name, sizeof(audit.tool) - 1);
        strncpy(audit.arguments_hash, args_hash, sizeof(audit.arguments_hash) - 1);
        strncpy(audit.error_message,
                err_buf[0] ? err_buf : "Schema validation failed",
                sizeof(audit.error_message) - 1);
        strncpy(audit.result_summary,
                "Arguments rejected by JSON schema",
                sizeof(audit.result_summary) - 1);
        ai_audit_log(&audit);

        csilk_json_t* err_obj = csilk_json_object();
        csilk_json_add_string(err_obj, "error", err_buf[0] ? err_buf : "schema validation failed");
        size_t len = 0;
        char*  err_str = csilk_json_serialize(err_obj, &len);
        csilk_json_free(err_obj);
        return err_str ? err_str : strdup("{\"error\":\"schema validation failed\"}");
    }

    /* 4. 业务校验 (Business Validator) */
    if (tool->validate) {
        if (tool->validate(tool, args, err_buf, sizeof(err_buf)) != 0) {
            ai_audit_record_t audit = {
                .stage = AI_AUDIT_STAGE_REJECTION,
                .actor_id = ctx->user_id,
                .session_id = ctx->session_id,
                .risk = tool->risk,
                .timestamp = (int64_t)time(NULL),
                .success = false,
            };
            strncpy(audit.tool, tool_name, sizeof(audit.tool) - 1);
            strncpy(audit.arguments_hash, args_hash, sizeof(audit.arguments_hash) - 1);
            strncpy(audit.error_message,
                    err_buf[0] ? err_buf : "Business validation failed",
                    sizeof(audit.error_message) - 1);
            strncpy(audit.result_summary,
                    "Arguments rejected by tool custom validator",
                    sizeof(audit.result_summary) - 1);
            ai_audit_log(&audit);

            csilk_json_t* err_obj = csilk_json_object();
            csilk_json_add_string(
                err_obj, "error", err_buf[0] ? err_buf : "business validation failed");
            size_t len = 0;
            char*  err_str = csilk_json_serialize(err_obj, &len);
            csilk_json_free(err_obj);
            return err_str ? err_str : strdup("{\"error\":\"business validation failed\"}");
        }
    }

    /* 5. 统一安全策略与风控评估 (Policy & Risk Evaluation) */
    ai_policy_decision_t* decision =
        ai_policy_evaluate(ctx->user_id, ctx->session_id, tool_name, args);
    if (!decision || !decision->allowed) {
        const char*       reason = decision ? decision->reason : "Policy rejection";
        ai_audit_record_t audit = {
            .stage = AI_AUDIT_STAGE_REJECTION,
            .actor_id = ctx->user_id,
            .session_id = ctx->session_id,
            .risk = decision ? decision->risk_level : tool->risk,
            .timestamp = (int64_t)time(NULL),
            .success = false,
        };
        strncpy(audit.tool, tool_name, sizeof(audit.tool) - 1);
        strncpy(audit.arguments_hash, args_hash, sizeof(audit.arguments_hash) - 1);
        strncpy(audit.error_message, reason, sizeof(audit.error_message) - 1);
        strncpy(audit.result_summary,
                "Operation blocked by Policy Engine",
                sizeof(audit.result_summary) - 1);
        ai_audit_log(&audit);

        csilk_json_t* err_obj = csilk_json_object();
        csilk_json_add_string(err_obj, "error", reason);
        size_t len = 0;
        char*  err_str = csilk_json_serialize(err_obj, &len);
        csilk_json_free(err_obj);
        if (decision) {
            ai_policy_decision_free(decision);
        }
        return err_str ? err_str : strdup("{\"error\":\"policy check failed\"}");
    }

    ai_risk_level_t effective_risk = decision->risk_level;
    ai_policy_decision_free(decision);

    /* 6. 参数规范化 (Normalize Args) */
    csilk_json_t* normalized = ai_tool_normalize_args(tool->parameters_schema, args);

    /* 7. 执行器调用 (Execution) */
    char* result = NULL;
    if (tool->execute) {
        result = tool->execute(tool, ctx, normalized);
    } else {
        result = strdup("{\"error\":\"tool executor not implemented\"}");
    }

    if (normalized) {
        csilk_json_free(normalized);
    }

    /* 8. 审计归档 (Audit) */
    bool              is_err = (result && strstr(result, "\"error\":") != NULL);
    ai_audit_record_t audit = {
        .stage = (strncmp(tool_name, "propose_", 8) == 0)
                     ? AI_AUDIT_STAGE_PROPOSAL
                     : ((strncmp(tool_name, "confirm_", 8) == 0) ? AI_AUDIT_STAGE_CONFIRMATION
                                                                 : AI_AUDIT_STAGE_EXECUTION),
        .actor_id = ctx->user_id,
        .session_id = ctx->session_id,
        .risk = effective_risk,
        .timestamp = (int64_t)time(NULL),
        .success = !is_err,
    };
    strncpy(audit.tool, tool_name, sizeof(audit.tool) - 1);
    strncpy(audit.arguments_hash, args_hash, sizeof(audit.arguments_hash) - 1);
    if (is_err) {
        strncpy(
            audit.error_message, "Tool returned error payload", sizeof(audit.error_message) - 1);
    }
    strncpy(audit.result_summary,
            is_err ? "Operation failed" : "Operation succeeded",
            sizeof(audit.result_summary) - 1);
    ai_audit_log(&audit);

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
