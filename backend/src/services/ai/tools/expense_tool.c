#include "services/ai/tools/expense_tool.h"
#include "services/ai/tools/tool_spec.h"
#include "services/ai/tools/tool_helpers.h"
#include "services/ai/policy/confirmation.h"
#include "application/account/usecases.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "common/balance.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include "core/ledger/ledger_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    const char* expense_type = args ? csilk_json_get_string(args, "expense_type") : NULL;
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
    csilk_json_t* list = mf_ai_repo_daily_expense_list(
        ctx->pool, ctx->user_id, page, page_size, start_date, end_date, 0, expense_type, &total);

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

    csilk_json_t* list =
        mf_ai_repo_daily_expense_monthly_by_category(ctx->pool, ctx->user_id, month_pat);

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
        ai_tool_today_str(date_buf, sizeof(date_buf));
        date = date_buf;
    }

    int64_t       matched_asset_id = 0;
    char          matched_asset_name[128] = "";
    int64_t       total_assets = 0;
    csilk_json_t* assets =
        mf_ai_repo_asset_list(ctx->pool, ctx->user_id, 1, 100, NULL, &total_assets);
    if (assets && csilk_json_is_array(assets)) {
        size_t asz = csilk_json_array_size(assets);
        for (size_t i = 0; i < asz; i++) {
            csilk_json_t* it = csilk_json_array_get(assets, i);
            const char*   name =
                csilk_json_get_string(it, "name") ? csilk_json_get_string(it, "name") : "";
            int64_t id = (int64_t)db_get_num(it, "id");
            if (i == 0 || (asset_name && asset_name[0] && ai_tool_str_icontains(name, asset_name))) {
                matched_asset_id = id;
                strncpy(matched_asset_name, name, sizeof(matched_asset_name) - 1);
                if (asset_name && asset_name[0] && ai_tool_str_icontains(name, asset_name)) {
                    break;
                }
            }
        }
        csilk_json_free(assets);
    }

    int64_t       matched_cat_id = 0;
    char          matched_cat_name[128] = "";
    csilk_json_t* cats = mf_ai_repo_category_list(ctx->pool, ctx->user_id, type);
    if (cats && csilk_json_is_array(cats)) {
        size_t csz = csilk_json_array_size(cats);
        for (size_t i = 0; i < csz; i++) {
            csilk_json_t* it = csilk_json_array_get(cats, i);
            const char*   name =
                csilk_json_get_string(it, "name") ? csilk_json_get_string(it, "name") : "";
            int64_t id = (int64_t)db_get_num(it, "id");
            if (i == 0 || (cat_name && cat_name[0] && ai_tool_str_icontains(name, cat_name))) {
                matched_cat_id = id;
                strncpy(matched_cat_name, name, sizeof(matched_cat_name) - 1);
                if (cat_name && cat_name[0] && ai_tool_str_icontains(name, cat_name)) {
                    break;
                }
            }
        }
        csilk_json_free(cats);
    }

    csilk_json_t* proposed_args = csilk_json_object();
    csilk_json_add_string(proposed_args, "type", type);
    csilk_json_add_number(proposed_args, "amount", amount);
    csilk_json_add_string(proposed_args, "date", date);
    csilk_json_add_number(proposed_args, "category_id", (double)matched_cat_id);
    csilk_json_add_number(proposed_args, "asset_id", (double)matched_asset_id);
    csilk_json_add_string(proposed_args, "note", note ? note : "");

    csilk_json_t* extra = csilk_json_object();
    csilk_json_add_string(extra, "category_name", matched_cat_name);
    csilk_json_add_string(extra, "asset_name", matched_asset_name);

    char* out = ai_tool_propose_finalize(ctx,
                                         "daily_expense",
                                         "confirm_proposed_expense",
                                         AI_RISK_HIGH,
                                         proposed_args,
                                         extra);
    csilk_json_free(proposed_args);
    csilk_json_free(extra);
    return out;
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

    if (amount <= 0.0) {
        return strdup("{\"error\":\"amount must be positive\"}");
    }

    char* verr = ai_tool_confirm_verify(ctx, "confirm_proposed_expense", args);
    if (verr) {
        return verr;
    }

    int64_t arg_lid = args ? db_get_int(args, "ledger_id") : 0;
    int64_t meta_lid = (ctx && ctx->metadata) ? db_get_int(ctx->metadata, "ledger_id") : 0;
    bool    meta_explicit =
        (ctx && ctx->metadata) ? csilk_json_get_bool(ctx->metadata, "ledger_id_explicit") : false;
    int64_t explicit_lid = arg_lid > 0 ? arg_lid : (meta_explicit ? meta_lid : 0);
    int64_t ledger_id = arg_lid > 0 ? arg_lid : meta_lid;

    if (asset_id > 0) {
        int64_t acct_lid = 0, acct_id = 0;
        if (asset_get_ledger_and_account(ctx->pool, asset_id, &acct_lid, &acct_id) != 0) {
            return strdup("{\"error\":\"asset not found\",\"code\":1003}");
        }
        if (explicit_lid > 0 && acct_lid > 0 && explicit_lid != acct_lid) {
            return strdup("{\"error\":\"Forbidden: 账本与资产所属账本不一致\",\"code\":1004}");
        }
        if (acct_lid > 0) {
            ledger_id = acct_lid;
        }
        int64_t chk_lid = ledger_id;
        if (chk_lid <= 0) {
            chk_lid = ledger_get_default(ctx->pool, ctx->user_id);
        }
        if (acct_id > 0 || chk_lid > 0) {
            int perm_chk = account_usecase_check_permission(
                ctx->pool, chk_lid, ctx->user_id, acct_id, MF_ACCOUNT_ACTION_WRITE_TX);
            if (perm_chk != 0) {
                return strdup("{\"error\":\"Forbidden: 关联账户无记账权限\",\"code\":1004}");
            }
        }
    } else if (ledger_id > 0) {
        int perm_chk = account_usecase_check_permission(
            ctx->pool, ledger_id, ctx->user_id, 0, MF_ACCOUNT_ACTION_WRITE_TX);
        if (perm_chk != 0) {
            return strdup("{\"error\":\"Forbidden: 账本无记账权限\",\"code\":1004}");
        }
    }

    db_tx_scope_t scope;
    if (db_tx_scope_begin(ctx->pool, "ai_confirm_expense", &scope) != 0) {
        return strdup("{\"error\":\"failed to begin transaction\"}");
    }

    const char* arg_cur = args ? csilk_json_get_string(args, "currency") : NULL;
    char        eff_cur[16] = "CNY";
    if (asset_id <= 0 ||
        asset_get_currency_for_user(ctx->pool, ctx->user_id, asset_id, eff_cur, sizeof(eff_cur)) !=
            0) {
        if (arg_cur && arg_cur[0]) {
            snprintf(eff_cur, sizeof(eff_cur), "%s", arg_cur);
        }
    }

    int64_t de_id = mf_ai_repo_daily_expense_insert(ctx->pool,
                                                    ctx->user_id,
                                                    ledger_id,
                                                    date,
                                                    type,
                                                    amount,
                                                    eff_cur,
                                                    category_id,
                                                    asset_id,
                                                    note ? note : "");
    if (de_id <= 0) {
        db_tx_scope_rollback(ctx->pool, &scope);
        return strdup("{\"error\":\"failed to insert daily expense\"}");
    }

    if (asset_id > 0) {
        int     is_income = (strcmp(type, "income") == 0);
        money_t amt_m;
        money_from_double(amount, currency_from_str(eff_cur), &amt_m);
        int rc = ledger_apply_expense_dated(ctx->pool,
                                            ctx->user_id,
                                            asset_id,
                                            amt_m,
                                            is_income,
                                            de_id,
                                            "AI 记账确认",
                                            (date && date[0]) ? date : NULL);
        if (rc != 0) {
            db_tx_scope_rollback(ctx->pool, &scope);
            return strdup("{\"error\":\"failed to update linked asset balance\"}");
        }
    }

    if (db_tx_scope_commit(ctx->pool, &scope) != 0) {
        db_tx_scope_rollback(ctx->pool, &scope);
        return strdup("{\"error\":\"failed to commit transaction\"}");
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "success", true);
    csilk_json_add_number(res, "id", (double)de_id);
    csilk_json_add_string(res, "message", "记账成功，账户余额已同步更新");

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static const ai_tool_spec_t s_expense_tool_specs[] = {
    {
        .name = "get_daily_expenses",
        .description = "查询当前用户的日常消费与收入流水列表",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_daily_expenses,
        .param_count = 5,
        .params = {
            AI_TOOL_PARAM("expense_type", "string", "筛选类型：expense 或 income"),
            AI_TOOL_PARAM("start_date", "string", "起始日期 YYYY-MM-DD"),
            AI_TOOL_PARAM("end_date", "string", "截止日期 YYYY-MM-DD"),
            AI_TOOL_PARAM("page", "integer", "当前页码，默认 1"),
            AI_TOOL_PARAM("page_size", "integer", "每页条数，默认 20"),
        },
    },
    {
        .name = "get_expense_by_category",
        .description = "按分类汇总统计指定月份的日常消费支出分布",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_expense_by_category,
        .param_count = 1,
        .params = {
            AI_TOOL_PARAM("month", "string", "月份前缀，如 2026-09"),
        },
    },
    {
        .name = "propose_daily_expense",
        .description = "日常记账拟录入，匹配账户与分类生成防篡改确认草案，返回待确认卡片数据。"
                       "成功后必须将返回的 draft_token/category_id/asset_id/amount/type/"
                       "date/note 填入 ```action 卡片（action_type: daily_expense）展示给用户",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_MEDIUM,
        .is_mutation = true,
        .requires_id = true,
        .execute = exec_propose_daily_expense,
        .param_count = 6,
        .params = {
            AI_TOOL_PARAM_REQ("amount", "number", "记账金额"),
            AI_TOOL_PARAM("type", "string", "类型：expense 或 income，默认 expense"),
            AI_TOOL_PARAM("category_name", "string", "消费分类名称（如 餐饮、购物、交通）"),
            AI_TOOL_PARAM("asset_name", "string", "支付账户名称（如 微信零钱、招行储蓄卡）"),
            AI_TOOL_PARAM("date", "string", "日期 YYYY-MM-DD，默认为今天"),
            AI_TOOL_PARAM("note", "string", "备注说明"),
        },
    },
    {
        .name = "confirm_proposed_expense",
        .description = "用户确认执行日常记账，验证确认令牌并原子入库与扣减账户余额",
        .action_type = "daily_expense",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_HIGH,
        .is_mutation = true,
        .requires_id = true,
        .internal_only = true,
        .execute = exec_confirm_proposed_expense,
        .param_count = 7,
        .params = {
            AI_TOOL_PARAM_REQ("amount", "number", "记账金额"),
            AI_TOOL_PARAM("type", "string", "类型：expense 或 income"),
            AI_TOOL_PARAM("date", "string", "记账日期 YYYY-MM-DD"),
            AI_TOOL_PARAM("category_id", "integer", "分类 ID"),
            AI_TOOL_PARAM("asset_id", "integer", "支付账户 ID"),
            AI_TOOL_PARAM("note", "string", "备注说明"),
            AI_TOOL_PARAM_REQ("draft_token", "string", "防伪确认令牌"),
        },
    },
};

void
ai_tool_expense_register_all(void)
{
    ai_tool_register_specs(s_expense_tool_specs,
                           sizeof(s_expense_tool_specs) / sizeof(s_expense_tool_specs[0]));
}
