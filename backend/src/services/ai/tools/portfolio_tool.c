#include "services/ai/tools/portfolio_tool.h"
#include "services/ai/tools/schema.h"
#include "repositories/asset_repo.h"
#include "repositories/daily_expense_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static char*
exec_analyze_financial_health(const ai_tool_t*         tool,
                              const ai_tool_context_t* ctx,
                              const csilk_json_t*      args)
{
    (void)tool;
    (void)args;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    int64_t       total = 0;
    csilk_json_t* list = asset_list(ctx->pool, ctx->user_id, 1, 200, NULL, &total);

    double liquid_cash = 0.0;
    double invested_assets = 0.0;
    double total_liabilities = 0.0;
    double total_assets = 0.0;

    if (list && csilk_json_is_array(list)) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   type = csilk_json_get_string(a, "asset_type") ?: "";
            double        val = db_get_num(a, "current_value");
            if (val == 0.0) {
                val = db_get_num(a, "balance");
            }

            if (strcmp(type, "cash") == 0 || strcmp(type, "bank") == 0) {
                liquid_cash += val;
                total_assets += val;
            } else if (strcmp(type, "stock") == 0 || strcmp(type, "fund") == 0 ||
                       strcmp(type, "crypto") == 0 || strcmp(type, "bond") == 0) {
                invested_assets += val;
                total_assets += val;
            } else if (strcmp(type, "loan") == 0 || strcmp(type, "credit_card") == 0 ||
                       strcmp(type, "other_liability") == 0) {
                total_liabilities += val;
            } else {
                total_assets += val;
            }
        }
        csilk_json_free(list);
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)ctx->user_id);
    const char*   p[] = {uid_str, NULL};
    csilk_json_t* burn_res =
        csilk_db_query_param_json(ctx->pool,
                                  "SELECT AVG(monthly_sum) as avg_burn FROM ("
                                  "  SELECT SUM(amount) as monthly_sum FROM daily_expenses "
                                  "  WHERE user_id = ? AND expense_type = 'expense' "
                                  "  GROUP BY substr(expense_date, 1, 7)"
                                  ")",
                                  p);
    double avg_monthly_burn = 0.0;
    if (burn_res && csilk_json_array_size(burn_res) > 0) {
        avg_monthly_burn = db_get_num(csilk_json_array_get(burn_res, 0), "avg_burn");
    }
    if (burn_res) {
        csilk_json_free(burn_res);
    }

    double runway_months = (avg_monthly_burn > 0.0) ? (liquid_cash / avg_monthly_burn)
                                                    : (liquid_cash > 0.0 ? 12.0 : 0.0);
    double debt_ratio = (total_assets > 0.0) ? (total_liabilities / total_assets) * 100.0 : 0.0;
    double invest_ratio = (total_assets > 0.0) ? (invested_assets / total_assets) * 100.0 : 0.0;

    double score = 50.0;
    if (runway_months >= 6.0) {
        score += 25.0;
    } else if (runway_months >= 3.0) {
        score += 15.0;
    }
    if (debt_ratio <= 30.0) {
        score += 15.0;
    } else if (debt_ratio <= 50.0) {
        score += 5.0;
    }
    if (invest_ratio >= 20.0 && invest_ratio <= 70.0) {
        score += 10.0;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "health_score", score > 100.0 ? 100.0 : score);
    csilk_json_add_number(res, "liquid_cash", liquid_cash);
    csilk_json_add_number(res, "avg_monthly_burn", avg_monthly_burn);
    csilk_json_add_number(res, "runway_months", runway_months);
    csilk_json_add_number(res, "invested_assets", invested_assets);
    csilk_json_add_number(res, "total_assets", total_assets);
    csilk_json_add_number(res, "total_liabilities", total_liabilities);
    csilk_json_add_number(res, "debt_ratio_pct", debt_ratio);
    csilk_json_add_number(res, "invest_ratio_pct", invest_ratio);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_calculate_compound_interest(const ai_tool_t*         tool,
                                 const ai_tool_context_t* ctx,
                                 const csilk_json_t*      args)
{
    (void)tool;
    (void)ctx;
    double principal = db_get_num(args, "principal");
    double rate_pct = db_get_num(args, "annual_rate_pct");
    double monthly_contrib = db_get_num(args, "monthly_contribution");
    int    years = (int)db_get_num(args, "years");
    if (years <= 0) {
        years = 10;
    }
    if (years > 50) {
        years = 50;
    }

    double r = (rate_pct / 100.0) / 12.0;
    double total_balance = principal;
    double total_principal = principal;

    csilk_json_t* schedule = csilk_json_array();
    for (int y = 1; y <= years; y++) {
        for (int m = 1; m <= 12; m++) {
            total_balance = total_balance * (1.0 + r) + monthly_contrib;
            total_principal += monthly_contrib;
        }
        csilk_json_t* point = csilk_json_object();
        csilk_json_add_number(point, "year", (double)y);
        csilk_json_add_number(point, "principal", total_principal);
        csilk_json_add_number(point, "interest", total_balance - total_principal);
        csilk_json_add_number(point, "balance", total_balance);
        csilk_json_array_append(schedule, point);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "final_balance", total_balance);
    csilk_json_add_number(res, "total_principal", total_principal);
    csilk_json_add_number(res, "total_interest", total_balance - total_principal);
    csilk_json_add_array(res, "yearly_schedule", schedule);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_calculate_loan_repayment(const ai_tool_t*         tool,
                              const ai_tool_context_t* ctx,
                              const csilk_json_t*      args)
{
    (void)tool;
    (void)ctx;
    double loan_amount = db_get_num(args, "loan_amount");
    double rate_pct = db_get_num(args, "annual_rate_pct");
    int    months = (int)db_get_num(args, "months");
    if (months <= 0) {
        months = 12;
    }

    double monthly_rate = (rate_pct / 100.0) / 12.0;
    double monthly_payment = 0.0;
    if (monthly_rate > 0.0) {
        monthly_payment = loan_amount * (monthly_rate * pow(1.0 + monthly_rate, months)) /
                          (pow(1.0 + monthly_rate, months) - 1.0);
    } else {
        monthly_payment = loan_amount / months;
    }

    double total_repayment = monthly_payment * months;
    double total_interest = total_repayment - loan_amount;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "loan_amount", loan_amount);
    csilk_json_add_number(res, "annual_rate_pct", rate_pct);
    csilk_json_add_number(res, "months", (double)months);
    csilk_json_add_number(res, "monthly_payment", monthly_payment);
    csilk_json_add_number(res, "total_interest", total_interest);
    csilk_json_add_number(res, "total_repayment", total_repayment);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

void
ai_tool_portfolio_register_all(void)
{
    /* 1. analyze_financial_health */
    {
        csilk_json_t* s = ai_schema_create_object();

        ai_tool_t t = {
            .name = "analyze_financial_health",
            .description =
                "全套核心财务健康体检诊断，计算备用金保障月数、净储蓄率、负债率与生息资产占比",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_analyze_financial_health,
        };
        ai_tool_register(&t);
    }

    /* 2. calculate_compound_interest */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "principal", "number", "初始本金");
        ai_schema_add_prop(s, "annual_rate_pct", "number", "预期年化收益率百分比，如 8.0");
        ai_schema_add_prop(s, "monthly_contribution", "number", "每月定投追加金额");
        ai_schema_add_prop(s, "years", "integer", "投资年限");
        ai_schema_add_required(s, "principal");
        ai_schema_add_required(s, "annual_rate_pct");

        ai_tool_t t = {
            .name = "calculate_compound_interest",
            .description = "计算长期复利增值模型与定期定投终值预测明细",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_calculate_compound_interest,
        };
        ai_tool_register(&t);
    }

    /* 3. calculate_loan_repayment */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "loan_amount", "number", "贷款本金总额");
        ai_schema_add_prop(s, "annual_rate_pct", "number", "贷款年化利率，如 4.2");
        ai_schema_add_prop(s, "months", "integer", "还款总期数（月）");
        ai_schema_add_required(s, "loan_amount");
        ai_schema_add_required(s, "annual_rate_pct");
        ai_schema_add_required(s, "months");

        ai_tool_t t = {
            .name = "calculate_loan_repayment",
            .description = "等额本息贷款月供测算器，计算月供金额、利息总支出与还款总成本",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_calculate_loan_repayment,
        };
        ai_tool_register(&t);
    }
}
