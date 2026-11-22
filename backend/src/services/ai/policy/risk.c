#include "services/ai/policy/risk.h"
#include "common/db.h"
#include <math.h>
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

static double
extract_object_amount(const csilk_json_t* obj)
{
    if (!obj || !csilk_json_is_object(obj)) {
        return 0.0;
    }

    static const char* const k_amount_keys[] = {
        "amount", "total_amount", "transfer_amount", "value", "cost", "fee", NULL};

    double max_field = 0.0;
    for (int i = 0; k_amount_keys[i]; i++) {
        double v = db_get_num(obj, k_amount_keys[i]);
        if (isfinite(v) && v > max_field) {
            max_field = v;
        }
    }

    double price = db_get_num(obj, "price");
    double qty = db_get_num(obj, "quantity");
    if (isfinite(price) && isfinite(qty) && price > 0.0 && qty > 0.0) {
        double notional = price * qty;
        if (isfinite(notional) && notional > max_field) {
            max_field = notional;
        }
    }

    return max_field;
}

void
ai_risk_extract_amounts(const csilk_json_t* args, double* out_max_amount, double* out_total_amount)
{
    double max_amt = 0.0;
    double total_amt = 0.0;

    if (args && csilk_json_is_object(args)) {
        max_amt = extract_object_amount(args);
        total_amt = max_amt;

        const csilk_json_t* actions = csilk_json_get(args, "actions");
        if (actions && csilk_json_is_array(actions)) {
            size_t count = csilk_json_array_size(actions);
            double batch_sum = 0.0;
            for (size_t i = 0; i < count; i++) {
                const csilk_json_t* item = csilk_json_array_get(actions, i);
                double              item_amt = extract_object_amount(item);

                const csilk_json_t* sub_arguments = csilk_json_get(item, "arguments");
                if (sub_arguments && csilk_json_is_object(sub_arguments)) {
                    double sub_amt = extract_object_amount(sub_arguments);
                    if (sub_amt > item_amt) {
                        item_amt = sub_amt;
                    }
                }
                const csilk_json_t* sub_args = csilk_json_get(item, "args");
                if (sub_args && csilk_json_is_object(sub_args)) {
                    double sub_amt = extract_object_amount(sub_args);
                    if (sub_amt > item_amt) {
                        item_amt = sub_amt;
                    }
                }

                if (item_amt > max_amt) {
                    max_amt = item_amt;
                }
                if (isfinite(item_amt) && item_amt > 0.0) {
                    batch_sum += item_amt;
                }
            }
            if (isfinite(batch_sum) && batch_sum > total_amt) {
                total_amt = batch_sum;
            }
        }
    }

    if (out_max_amount) {
        *out_max_amount = max_amt;
    }
    if (out_total_amount) {
        *out_total_amount = total_amt;
    }
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

    /* 2. 检查交易/划转金额大小（含扩展字段及批处理累计额），动态升级风险 */
    double max_amount = 0.0;
    double total_amount = 0.0;
    ai_risk_extract_amounts(args, &max_amount, &total_amount);
    double amount = total_amount > max_amount ? total_amount : max_amount;

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
