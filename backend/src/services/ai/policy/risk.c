#include "services/ai/policy/risk.h"
#include "common/db.h"
#include <string.h>

const char*
ai_risk_level_to_string(ai_risk_level_t level)
{
    switch (level) {
    case AI_RISK_READ_ONLY:
        return "READ_ONLY";
    case AI_RISK_LOW:
        return "LOW";
    case AI_RISK_MEDIUM:
        return "MEDIUM";
    case AI_RISK_HIGH:
        return "HIGH";
    case AI_RISK_CRITICAL:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}

ai_risk_level_t
ai_risk_level_from_string(const char* str)
{
    if (!str) {
        return AI_RISK_LOW;
    }
    if (strcmp(str, "READ_ONLY") == 0 || strcmp(str, "read_only") == 0) {
        return AI_RISK_READ_ONLY;
    }
    if (strcmp(str, "LOW") == 0 || strcmp(str, "low") == 0) {
        return AI_RISK_LOW;
    }
    if (strcmp(str, "MEDIUM") == 0 || strcmp(str, "medium") == 0) {
        return AI_RISK_MEDIUM;
    }
    if (strcmp(str, "HIGH") == 0 || strcmp(str, "high") == 0) {
        return AI_RISK_HIGH;
    }
    if (strcmp(str, "CRITICAL") == 0 || strcmp(str, "critical") == 0) {
        return AI_RISK_CRITICAL;
    }
    return AI_RISK_LOW;
}

ai_risk_level_t
ai_risk_assess(const char* tool_name, const csilk_json_t* args)
{
    if (!tool_name || !tool_name[0]) {
        return AI_RISK_READ_ONLY;
    }

    /* 1. 明确的极高风险工具（破坏性操作） */
    if (strcmp(tool_name, "delete_asset") == 0 || strcmp(tool_name, "reset_account") == 0 ||
        strcmp(tool_name, "clear_all_data") == 0 || strcmp(tool_name, "truncate_tables") == 0) {
        return AI_RISK_CRITICAL;
    }

    /* 2. 检查交易/划转金额大小，动态升降级风险 */
    double amount = 0.0;
    if (args) {
        amount = db_get_num(args, "amount");
    }

    /* 3. 执行确认类动账操作 (Execution) */
    if (strncmp(tool_name, "confirm_", 8) == 0 || strcmp(tool_name, "execute_transfer") == 0 ||
        strcmp(tool_name, "execute_trade") == 0) {
        if (amount >= 50000.0) {
            return AI_RISK_CRITICAL; /* 大额资金操作 -> CRITICAL */
        }
        return AI_RISK_HIGH;         /* 常规确认执行 -> HIGH */
    }

    /* 4. 拟录入类草案生成操作 (Proposal) */
    if (strncmp(tool_name, "propose_", 8) == 0 || strncmp(tool_name, "draft_", 6) == 0) {
        if (amount >= 50000.0) {
            return AI_RISK_HIGH; /* 大额拟录入 -> HIGH */
        }
        return AI_RISK_MEDIUM;   /* 常规草案 -> MEDIUM */
    }

    /* 5. 试算与辅助类计算工具 */
    if (strncmp(tool_name, "calculate_", 10) == 0 || strcmp(tool_name, "web_search") == 0 ||
        strcmp(tool_name, "analyze_financial_health") == 0) {
        return AI_RISK_LOW;
    }

    /* 6. 查询类工具 */
    if (strncmp(tool_name, "get_", 4) == 0 || strncmp(tool_name, "query_", 6) == 0 ||
        strncmp(tool_name, "list_", 5) == 0) {
        return AI_RISK_READ_ONLY;
    }

    return AI_RISK_LOW;
}
