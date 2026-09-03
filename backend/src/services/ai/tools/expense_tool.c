#include "services/ai/tools/expense_tool.h"
#include "services/ai/tools/schema.h"
#include "services/ai/policy/confirmation.h"
#include "repositories/daily_expense_repo.h"
#include "repositories/category_repo.h"
#include "repositories/asset_repo.h"
#include "common/balance.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

static int
str_icontains(const char* haystack, const char* needle)
{
    if (!haystack || !needle) {
        return 0;
    }
    if (!needle[0]) {
        return 1;
    }
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) {
        return 0;
    }
    char h_small[256], n_small[256];
    if (hlen < sizeof(h_small) && nlen < sizeof(n_small)) {
        for (size_t i = 0; i < hlen; i++) {
            h_small[i] = (char)tolower((unsigned char)haystack[i]);
        }
        h_small[hlen] = '\0';
        for (size_t i = 0; i < nlen; i++) {
            n_small[i] = (char)tolower((unsigned char)needle[i]);
        }
        n_small[nlen] = '\0';
        return strstr(h_small, n_small) != NULL;
    }
    return strstr(haystack, needle) != NULL;
}

static char*
exec_get_daily_expenses(const ai_tool_t*         tool,
                        const ai_tool_context_t* ctx,
                        const csilk_json_t*      args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    const char* start_date = args ? csilk_json_get_string(args, "start_date") : NULL;
    const char* end_date = args ? csilk_json_get_string(args, "end_date") : NULL;
    const char* type = args ? csilk_json_get_string(args, "expense_type") : NULL;
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
    csilk_json_t* list = de_list(
        ctx->pool, ctx->user_id, page, page_size, type, NULL, NULL, start_date, end_date, &total);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total", (double)total);
    csilk_json_add_number(res, "page", (double)page);
    csilk_json_add_number(res, "page_size", (double)page_size);
    if (list) {
        csilk_json_add_array(res, "expenses", list);
    } else {
        csilk_json_add_array(res, "expenses", csilk_json_array());
    }

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_get_expense_by_category(const ai_tool_t*         tool,
                             const ai_tool_context_t* ctx,
                             const csilk_json_t*      args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    char        month_pat[32] = {0};
    const char* m_in = args ? csilk_json_get_string(args, "month") : NULL;
    if (m_in && m_in[0]) {
        snprintf(month_pat, sizeof(month_pat), "%s%%", m_in);
    } else {
        time_t    now = time(NULL);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        snprintf(
            month_pat, sizeof(month_pat), "%04d-%02d%%", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1);
    }

    csilk_json_t* list = de_monthly_by_category(ctx->pool, ctx->user_id, month_pat);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "month_pattern", month_pat);
    if (list) {
        csilk_json_add_array(res, "categories", list);
    } else {
        csilk_json_add_array(res, "categories", csilk_json_array());
    }

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_propose_daily_expense(const ai_tool_t*         tool,
                           const ai_tool_context_t* ctx,
                           const csilk_json_t*      args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    const char* type = args ? csilk_json_get_string(args, "type") : "expense";
    if (!type || !type[0]) {
        type = "expense";
    }
    double      amount = db_get_num(args, "amount");
    const char* cat_name = args ? csilk_json_get_string(args, "category_name") : "";
    const char* asset_name = args ? csilk_json_get_string(args, "asset_name") : "";
    const char* date = args ? csilk_json_get_string(args, "date") : NULL;
    const char* note = args ? csilk_json_get_string(args, "note") : "";

    if (amount <= 0.0) {
        return strdup("{\"error\":\"amount must be positive\"}");
    }

    char date_buf[32];
    if (!date || !date[0]) {
        time_t    now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        snprintf(date_buf,
                 sizeof(date_buf),
                 "%04d-%02d-%02d",
                 tm_now.tm_year + 1900,
                 tm_now.tm_mon + 1,
                 tm_now.tm_mday);
        date = date_buf;
    }

    int64_t       matched_asset_id = 0;
    char          matched_asset_name[128] = "";
    int64_t       total_assets = 0;
    csilk_json_t* assets = asset_list(ctx->pool, ctx->user_id, 1, 100, NULL, &total_assets);
    if (assets && csilk_json_is_array(assets)) {
        size_t asz = csilk_json_array_size(assets);
        for (size_t i = 0; i < asz; i++) {
            csilk_json_t* it = csilk_json_array_get(assets, i);
            const char*   name = csilk_json_get_string(it, "name") ?: "";
            int64_t       id = (int64_t)db_get_num(it, "id");
            if (i == 0 || (asset_name && asset_name[0] && str_icontains(name, asset_name))) {
                matched_asset_id = id;
                strncpy(matched_asset_name, name, sizeof(matched_asset_name) - 1);
                if (asset_name && asset_name[0] && str_icontains(name, asset_name)) {
                    break;
                }
            }
        }
        csilk_json_free(assets);
    }

    int64_t       matched_cat_id = 0;
    char          matched_cat_name[128] = "";
    csilk_json_t* cats = category_list(ctx->pool, ctx->user_id, type);
    if (cats && csilk_json_is_array(cats)) {
        size_t csz = csilk_json_array_size(cats);
        for (size_t i = 0; i < csz; i++) {
            csilk_json_t* it = csilk_json_array_get(cats, i);
            const char*   name = csilk_json_get_string(it, "name") ?: "";
            int64_t       id = (int64_t)db_get_num(it, "id");
            if (i == 0 || (cat_name && cat_name[0] && str_icontains(name, cat_name))) {
                matched_cat_id = id;
                strncpy(matched_cat_name, name, sizeof(matched_cat_name) - 1);
                if (cat_name && cat_name[0] && str_icontains(name, cat_name)) {
                    break;
                }
            }
        }
        csilk_json_free(cats);
    }

    char* draft_token = ai_confirmation_create_token(ctx->user_id, amount, type, date);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "propose_success", true);
    csilk_json_add_string(res, "type", type);
    csilk_json_add_number(res, "amount", amount);
    csilk_json_add_string(res, "date", date);
    csilk_json_add_number(res, "category_id", (double)matched_cat_id);
    csilk_json_add_string(res, "category_name", matched_cat_name);
    csilk_json_add_number(res, "asset_id", (double)matched_asset_id);
    csilk_json_add_string(res, "asset_name", matched_asset_name);
    csilk_json_add_string(res, "note", note ? note : "");
    csilk_json_add_string(res, "draft_token", draft_token ? draft_token : "");

    if (draft_token) {
        free(draft_token);
    }

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_confirm_proposed_expense(const ai_tool_t*         tool,
                              const ai_tool_context_t* ctx,
                              const csilk_json_t*      args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    const char* type = args ? csilk_json_get_string(args, "type") : "expense";
    if (!type || !type[0]) {
        type = "expense";
    }
    double      amount = db_get_num(args, "amount");
    const char* date = args ? csilk_json_get_string(args, "date") : "";
    int64_t     category_id = (int64_t)db_get_num(args, "category_id");
    int64_t     asset_id = (int64_t)db_get_num(args, "asset_id");
    const char* note = args ? csilk_json_get_string(args, "note") : "";
    const char* draft_token = args ? csilk_json_get_string(args, "draft_token") : "";

    if (amount <= 0.0) {
        return strdup("{\"error\":\"amount must be positive\"}");
    }

    if (!draft_token || !draft_token[0] ||
        !ai_confirmation_verify_token(ctx->user_id, amount, type, date, draft_token)) {
        return strdup("{\"error\":\"invalid or expired confirmation draft token\"}");
    }

    csilk_db_exec(ctx->pool, "BEGIN TRANSACTION");

    int64_t de_id = de_insert(ctx->pool,
                              ctx->user_id,
                              category_id,
                              asset_id,
                              type,
                              amount,
                              "CNY",
                              date,
                              note ? note : "");
    if (de_id <= 0) {
        csilk_db_exec(ctx->pool, "ROLLBACK");
        return strdup("{\"error\":\"failed to insert daily expense\"}");
    }

    if (asset_id > 0) {
        double delta = (strcmp(type, "income") == 0) ? amount : -amount;
        int    rc = balance_apply_delta(
            ctx->pool, asset_id, ctx->user_id, delta, "daily_expense", de_id, "AI 记账确认");
        if (rc != 0) {
            csilk_db_exec(ctx->pool, "ROLLBACK");
            return strdup("{\"error\":\"failed to update linked asset balance\"}");
        }
    }

    csilk_db_exec(ctx->pool, "COMMIT");

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "success", true);
    csilk_json_add_number(res, "id", (double)de_id);
    csilk_json_add_string(res, "message", "记账成功，账户余额已同步更新");

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

void
ai_tool_expense_register_all(void)
{
    /* 1. get_daily_expenses */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "expense_type", "string", "筛选类型：expense 或 income");
        ai_schema_add_prop(s, "start_date", "string", "起始日期 YYYY-MM-DD");
        ai_schema_add_prop(s, "end_date", "string", "截止日期 YYYY-MM-DD");
        ai_schema_add_prop(s, "page", "integer", "当前页码，默认 1");
        ai_schema_add_prop(s, "page_size", "integer", "每页条数，默认 20");

        ai_tool_t t = {
            .name = "get_daily_expenses",
            .description = "查询当前用户的日常消费与收入流水列表",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_get_daily_expenses,
        };
        ai_tool_register(&t);
    }

    /* 2. get_expense_by_category */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "month", "string", "月份前缀，如 2026-09");

        ai_tool_t t = {
            .name = "get_expense_by_category",
            .description = "按分类汇总统计指定月份的日常消费支出分布",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_get_expense_by_category,
        };
        ai_tool_register(&t);
    }

    /* 3. propose_daily_expense */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "amount", "number", "记账金额");
        ai_schema_add_prop(s, "type", "string", "类型：expense 或 income，默认 expense");
        ai_schema_add_prop(s, "category_name", "string", "消费分类名称（如 餐饮、购物、交通）");
        ai_schema_add_prop(s, "asset_name", "string", "支付账户名称（如 微信零钱、招行储蓄卡）");
        ai_schema_add_prop(s, "date", "string", "日期 YYYY-MM-DD，默认为今天");
        ai_schema_add_prop(s, "note", "string", "备注说明");
        ai_schema_add_required(s, "amount");

        ai_tool_t t = {
            .name = "propose_daily_expense",
            .description = "日常记账拟录入，匹配账户与分类生成防篡改确认草案，返回待确认卡片数据",
            .parameters_schema = s,
            .permission = AI_PERM_WRITE,
            .risk = AI_RISK_MEDIUM,
            .is_mutation = true,
            .validate = NULL,
            .execute = exec_propose_daily_expense,
        };
        ai_tool_register(&t);
    }

    /* 4. confirm_proposed_expense */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "amount", "number", "记账金额");
        ai_schema_add_prop(s, "type", "string", "类型：expense 或 income");
        ai_schema_add_prop(s, "date", "string", "记账日期 YYYY-MM-DD");
        ai_schema_add_prop(s, "category_id", "integer", "分类 ID");
        ai_schema_add_prop(s, "asset_id", "integer", "支付账户 ID");
        ai_schema_add_prop(s, "note", "string", "备注说明");
        ai_schema_add_prop(s, "draft_token", "string", "防伪确认令牌");
        ai_schema_add_required(s, "amount");
        ai_schema_add_required(s, "draft_token");

        ai_tool_t t = {
            .name = "confirm_proposed_expense",
            .description = "用户确认执行日常记账，验证确认令牌并原子入库与扣减账户余额",
            .parameters_schema = s,
            .permission = AI_PERM_WRITE,
            .risk = AI_RISK_HIGH,
            .is_mutation = true,
            .validate = NULL,
            .execute = exec_confirm_proposed_expense,
        };
        ai_tool_register(&t);
    }
}
