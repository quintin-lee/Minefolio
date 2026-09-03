#include "services/ai/tools/transaction_tool.h"
#include "services/ai/tools/schema.h"
#include "repositories/transaction_repo.h"
#include "repositories/daily_expense_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char*
exec_get_transactions(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    const char* type = args ? csilk_json_get_string(args, "transaction_type") : NULL;
    int64_t     page = 1;
    int64_t     page_size = 20;
    if (args) {
        double p = db_get_num(args, "page");
        if (p >= 1.0) {
            page = (int64_t)p;
        }
        double ps = db_get_num(args, "page_size");
        if (ps >= 1.0 && ps <= 100.0) {
            page_size = (int64_t)ps;
        }
    }

    int64_t       total = 0;
    csilk_json_t* list = tx_list(
        ctx->pool, ctx->user_id, page, page_size, NULL, NULL, type, NULL, NULL, NULL, &total);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total", (double)total);
    csilk_json_add_number(res, "page", (double)page);
    csilk_json_add_number(res, "page_size", (double)page_size);
    if (list) {
        csilk_json_add_array(res, "transactions", list);
    } else {
        csilk_json_add_array(res, "transactions", csilk_json_array());
    }

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_get_expense_trend(const ai_tool_t*         tool,
                       const ai_tool_context_t* ctx,
                       const csilk_json_t*      args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    int months = 6;
    if (args) {
        double m = db_get_num(args, "months");
        if (m >= 1.0 && m <= 24.0) {
            months = (int)m;
        }
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)ctx->user_id);
    const char* p[] = {uid_str, NULL};

    csilk_json_t* trend = csilk_db_query_param_json(
        ctx->pool,
        "SELECT substr(expense_date, 1, 7) as month, "
        "       SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END) as income, "
        "       SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END) as expense "
        "FROM daily_expenses WHERE user_id=? "
        "GROUP BY substr(expense_date, 1, 7) "
        "ORDER BY month DESC LIMIT 24",
        p);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "months", (double)months);
    if (trend) {
        csilk_json_add_array(res, "trend", trend);
    } else {
        csilk_json_add_array(res, "trend", csilk_json_array());
    }

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

void
ai_tool_transaction_register_all(void)
{
    /* 1. get_transactions */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(
            s,
            "transaction_type",
            "string",
            "交易类型：income, expense, transfer_in, transfer_out, buy, sell, dividend");
        ai_schema_add_prop(s, "page", "integer", "当前页码，默认 1");
        ai_schema_add_prop(s, "page_size", "integer", "每页条数，默认 20");

        ai_tool_t t = {
            .name = "get_transactions",
            .description = "查询当前用户的交易明细流水，支持按交易类型、日期与分页筛选",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_get_transactions,
        };
        ai_tool_register(&t);
    }

    /* 2. get_expense_trend */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "months", "integer", "查询过去 N 个月的收支走势，默认 6");

        ai_tool_t t = {
            .name = "get_expense_trend",
            .description = "查询过去若干月的月度收支走势、储蓄结余与月均消费基准",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_get_expense_trend,
        };
        ai_tool_register(&t);
    }
}
