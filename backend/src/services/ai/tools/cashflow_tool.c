#include "services/ai/tools/cashflow_tool.h"
#include "services/ai/tools/tool_spec.h"
#include "services/ai/tools/tool_helpers.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char*
exec_get_summary(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    (void)args;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    int64_t       total_assets_cnt = 0;
    csilk_json_t* list =
        mf_ai_repo_asset_list(ctx->pool, ctx->user_id, 1, 200, NULL, &total_assets_cnt);

    double total_assets = 0.0;
    double total_liabilities = 0.0;
    if (list && csilk_json_is_array(list)) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   type = csilk_json_get_string(a, "asset_type")
                                     ? csilk_json_get_string(a, "asset_type")
                                     : "";
            double        val = db_get_num(a, "current_value");
            if (val == 0.0) {
                val = db_get_num(a, "balance");
            }
            if (strcmp(type, "loan") == 0 || strcmp(type, "credit_card") == 0 ||
                strcmp(type, "other_liability") == 0) {
                total_liabilities += val;
            } else {
                total_assets += val;
            }
        }
        csilk_json_free(list);
    }

    char      month_pat[32];
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(month_pat, sizeof(month_pat), "%04d-%02d%%", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1);

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)ctx->user_id);
    const char*   p_exp[] = {uid_str, month_pat, uid_str, uid_str, NULL};
    csilk_json_t* exp_res = csilk_db_query_param_json(
        ctx->pool,
        "SELECT COALESCE(SUM(de.amount), 0) as total FROM daily_expenses de "
        "LEFT JOIN assets _ra ON de.asset_id = _ra.id "
        "WHERE de.user_id=? AND de.expense_type='expense' "
        "AND CAST(de.expense_date AS TEXT) LIKE ? AND " MF_RBAC_ASSET_UNMASKED_SQL("_ra"),
        p_exp);
    double cur_expense = 0.0;
    if (exp_res && csilk_json_array_size(exp_res) > 0) {
        cur_expense = db_get_num(csilk_json_array_get(exp_res, 0), "total");
    }
    if (exp_res) {
        csilk_json_free(exp_res);
    }

    csilk_json_t* inc_res = csilk_db_query_param_json(
        ctx->pool,
        "SELECT COALESCE(SUM(de.amount), 0) as total FROM daily_expenses de "
        "LEFT JOIN assets _ra ON de.asset_id = _ra.id "
        "WHERE de.user_id=? AND de.expense_type='income' "
        "AND CAST(de.expense_date AS TEXT) LIKE ? AND " MF_RBAC_ASSET_UNMASKED_SQL("_ra"),
        p_exp);
    double cur_income = 0.0;
    if (inc_res && csilk_json_array_size(inc_res) > 0) {
        cur_income = db_get_num(csilk_json_array_get(inc_res, 0), "total");
    }
    if (inc_res) {
        csilk_json_free(inc_res);
    }

    double net_worth = total_assets - total_liabilities;
    double net_savings = cur_income - cur_expense;
    double savings_rate = cur_income > 0.0 ? (net_savings / cur_income) * 100.0 : 0.0;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total_assets", total_assets);
    csilk_json_add_number(res, "total_liabilities", total_liabilities);
    csilk_json_add_number(res, "net_worth", net_worth);
    csilk_json_add_number(res, "monthly_income", cur_income);
    csilk_json_add_number(res, "monthly_expense", cur_expense);
    csilk_json_add_number(res, "net_savings", net_savings);
    csilk_json_add_number(res, "savings_rate_pct", savings_rate);
    return ai_tool_json_serialize_free(res);
}

static char*
exec_get_exchange_rate(const ai_tool_t*         tool,
                       const ai_tool_context_t* ctx,
                       const csilk_json_t*      args)
{
    (void)tool;
    const char* base = args ? csilk_json_get_string(args, "base_currency") : "USD";
    const char* target = args ? csilk_json_get_string(args, "target_currency") : "CNY";
    if (!base || !base[0]) {
        base = "USD";
    }
    if (!target || !target[0]) {
        target = "CNY";
    }

    double rate = 7.20;
    if (strcmp(base, "USD") == 0 && strcmp(target, "CNY") == 0) {
        rate = 7.25;
    } else if (strcmp(base, "CNY") == 0 && strcmp(target, "USD") == 0) {
        rate = 1.0 / 7.25;
    } else if (strcmp(base, "HKD") == 0 && strcmp(target, "CNY") == 0) {
        rate = 0.92;
    } else if (strcmp(base, target) == 0) {
        rate = 1.0;
    }

    if (ctx && ctx->pool) {
        const char*   p[] = {base, target, NULL};
        csilk_json_t* q =
            csilk_db_query_param_json(ctx->pool,
                                      "SELECT rate FROM exchange_rates WHERE base_currency=? AND "
                                      "target_currency=? ORDER BY id DESC LIMIT 1",
                                      p);
        if (q && csilk_json_array_size(q) > 0) {
            double db_r = db_get_num(csilk_json_array_get(q, 0), "rate");
            if (db_r > 0.0) {
                rate = db_r;
            }
        }
        if (q) {
            csilk_json_free(q);
        }
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "base_currency", base);
    csilk_json_add_string(res, "target_currency", target);
    csilk_json_add_number(res, "rate", rate);
    return ai_tool_json_serialize_free(res);
}

static const ai_tool_spec_t s_cashflow_tool_specs[] = {
    {
        .name = "get_summary",
        .description = "查询当前用户的总净资产、总资产、总负债与当月总收支宏观概览",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_summary,
    },
    {
        .name = "get_exchange_rate",
        .description = "查询多币种外汇即时参考汇率与折算基准",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_exchange_rate,
        .param_count = 2,
        .params = {
            AI_TOOL_PARAM("base_currency", "string", "基础币种，如 USD, HKD"),
            AI_TOOL_PARAM("target_currency", "string", "目标币种，如 CNY, USD"),
        },
    },
};

void
ai_tool_cashflow_register_all(void)
{
    ai_tool_register_specs(s_cashflow_tool_specs,
                           sizeof(s_cashflow_tool_specs) / sizeof(s_cashflow_tool_specs[0]));
}
