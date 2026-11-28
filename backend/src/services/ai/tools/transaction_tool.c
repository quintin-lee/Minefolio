#include "services/ai/tools/transaction_tool.h"
#include "services/ai/tools/tool_spec.h"
#include "services/ai/tools/tool_helpers.h"
#include "application/account/usecases.h"
#include "application/transaction/usecases.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char*
exec_get_transactions(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    const char* type = args ? csilk_json_get_string(args, "transaction_type") : NULL;
    int64_t     page = 1, page_size = 20;
    ai_tool_parse_pagination(args, 20, 100, &page, &page_size);

    int64_t       total = 0;
    csilk_json_t* list =
        mf_ai_repo_transaction_list(ctx->pool, ctx->user_id, page, page_size, type, &total);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total", (double)total);
    csilk_json_add_number(res, "page", (double)page);
    csilk_json_add_number(res, "page_size", (double)page_size);
    csilk_json_add_array(res, "transactions", list ? list : csilk_json_array());
    return ai_tool_json_serialize_free(res);
}

static char*
exec_get_expense_trend(const ai_tool_t*         tool,
                       const ai_tool_context_t* ctx,
                       const csilk_json_t*      args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
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
    const char* p[] = {uid_str, uid_str, uid_str, NULL};

    csilk_json_t* trend = csilk_db_query_param_json(
        ctx->pool,
        "SELECT substr(CAST(de.expense_date AS TEXT), 1, 7) as month, "
        "       SUM(CASE WHEN de.expense_type='income' THEN de.amount ELSE 0 END) as income, "
        "       SUM(CASE WHEN de.expense_type='expense' THEN de.amount ELSE 0 END) as expense "
        "FROM daily_expenses de LEFT JOIN assets _ra ON de.asset_id = _ra.id "
        "WHERE de.user_id=? AND " MF_RBAC_ASSET_UNMASKED_SQL(
            "_ra") " "
                   "GROUP BY substr(CAST(de.expense_date AS TEXT), 1, 7) "
                   "ORDER BY month DESC LIMIT 24",
        p);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "months", (double)months);
    csilk_json_add_array(res, "trend", trend ? trend : csilk_json_array());
    return ai_tool_json_serialize_free(res);
}

static bool
match_asset_strict(const csilk_json_t* assets,
                   int64_t             provided_id,
                   const char*         provided_name,
                   int64_t*            out_id,
                   char*               out_name,
                   size_t              out_name_sz)
{
    if (out_id) {
        *out_id = 0;
    }
    if (out_name && out_name_sz > 0) {
        out_name[0] = '\0';
    }
    if (!assets || !csilk_json_is_array(assets)) {
        return false;
    }
    size_t asz = csilk_json_array_size(assets);
    if (provided_id > 0) {
        for (size_t i = 0; i < asz; i++) {
            const csilk_json_t* it = csilk_json_array_get(assets, i);
            int64_t             id = (int64_t)db_get_num(it, "id");
            if (id == provided_id) {
                if (out_id) {
                    *out_id = id;
                }
                const char* n = csilk_json_get_string(it, "name");
                if (out_name && out_name_sz > 0) {
                    snprintf(out_name, out_name_sz, "%s", n ? n : "");
                }
                return true;
            }
        }
    }
    if (provided_name && provided_name[0]) {
        for (size_t i = 0; i < asz; i++) {
            const csilk_json_t* it = csilk_json_array_get(assets, i);
            const char*         n = csilk_json_get_string(it, "name");
            if (n && ai_tool_str_icontains(n, provided_name)) {
                if (out_id) {
                    *out_id = (int64_t)db_get_num(it, "id");
                }
                if (out_name && out_name_sz > 0) {
                    snprintf(out_name, out_name_sz, "%s", n);
                }
                return true;
            }
        }
    }
    return false;
}

static char*
exec_propose_transaction(const ai_tool_t*         tool,
                         const ai_tool_context_t* ctx,
                         const csilk_json_t*      args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    const char* tx_type = args ? csilk_json_get_string(args, "transaction_type") : NULL;
    double      amount = args ? db_get_num(args, "amount") : 0.0;
    int64_t     asset_id_in = args ? db_get_int(args, "asset_id") : 0;
    const char* asset_name_in = args ? csilk_json_get_string(args, "asset_name") : NULL;
    int64_t     linked_id_in = args ? db_get_int(args, "linked_asset_id") : 0;
    const char* linked_name_in = args ? csilk_json_get_string(args, "linked_asset_name") : NULL;
    double      quantity = args ? db_get_num(args, "quantity") : 0.0;
    double      price = args ? db_get_num(args, "price") : 0.0;
    double      fee = args ? db_get_num(args, "fee") : 0.0;
    const char* date = args ? csilk_json_get_string(args, "date") : NULL;
    const char* note = args ? csilk_json_get_string(args, "note") : NULL;

    if (!tx_type || !tx_type[0]) {
        return ai_tool_json_error("transaction_type is required", 1002);
    }
    if (amount <= 0.0) {
        return ai_tool_json_error("amount must be positive", 1002);
    }
    if (quantity < 0.0 || price < 0.0 || fee < 0.0) {
        return ai_tool_json_error("quantity, price and fee cannot be negative", 1002);
    }
    if (quantity > 0.0 && price <= 0.0) {
        price = amount / quantity;
    } else if (price > 0.0 && quantity <= 0.0) {
        quantity = amount / price;
    }

    char date_buf[32];
    if (!date || !date[0]) {
        ai_tool_today_str(date_buf, sizeof(date_buf));
        date = date_buf;
    }

    int64_t       total_assets = 0;
    csilk_json_t* assets =
        mf_ai_repo_asset_list(ctx->pool, ctx->user_id, 1, 200, NULL, &total_assets);

    int64_t matched_asset_id = 0;
    char    matched_asset_name[128] = {0};
    if (!match_asset_strict(assets, asset_id_in, asset_name_in, &matched_asset_id,
                            matched_asset_name, sizeof(matched_asset_name))) {
        if (assets) {
            csilk_json_free(assets);
        }
        return ai_tool_json_error("未匹配到目标资产，请先通过 get_assets 确认 asset_id 或 asset_name",
                                  1003);
    }

    int64_t matched_linked_id = 0;
    char    matched_linked_name[128] = {0};
    if (linked_id_in > 0 || (linked_name_in && linked_name_in[0])) {
        if (!match_asset_strict(assets, linked_id_in, linked_name_in, &matched_linked_id,
                                matched_linked_name, sizeof(matched_linked_name))) {
            if (assets) {
                csilk_json_free(assets);
            }
            return ai_tool_json_error("未匹配到关联资金账户", 1003);
        }
        if (matched_linked_id == matched_asset_id) {
            if (assets) {
                csilk_json_free(assets);
            }
            return ai_tool_json_error("关联资金账户不能与目标资产相同", 1002);
        }
    }
    if (assets) {
        csilk_json_free(assets);
    }

    csilk_json_t* proposed_args = csilk_json_object();
    csilk_json_add_string(proposed_args, "transaction_type", tx_type);
    csilk_json_add_number(proposed_args, "amount", amount);
    csilk_json_add_number(proposed_args, "asset_id", (double)matched_asset_id);
    csilk_json_add_number(proposed_args, "linked_asset_id", (double)matched_linked_id);
    csilk_json_add_number(proposed_args, "quantity", quantity);
    csilk_json_add_number(proposed_args, "price", price);
    csilk_json_add_number(proposed_args, "fee", fee);
    csilk_json_add_string(proposed_args, "date", date);
    csilk_json_add_string(proposed_args, "note", note ? note : "");

    csilk_json_t* extra = csilk_json_object();
    csilk_json_add_string(extra, "asset_name", matched_asset_name);
    csilk_json_add_string(extra, "linked_asset_name", matched_linked_name);

    ai_risk_level_t risk = (amount >= 50000.0) ? AI_RISK_CRITICAL : AI_RISK_HIGH;
    char*           out = ai_tool_propose_finalize(
        ctx, "transaction", "confirm_proposed_transaction", risk, proposed_args, extra);
    csilk_json_free(proposed_args);
    csilk_json_free(extra);
    return out;
}

static char*
exec_confirm_proposed_transaction(const ai_tool_t*         tool,
                                  const ai_tool_context_t* ctx,
                                  const csilk_json_t*      args)
{
    (void)tool;
    char* verr = ai_tool_confirm_verify(ctx, "confirm_proposed_transaction", args);
    if (verr) {
        return verr;
    }

    const char* tx_type = args ? csilk_json_get_string(args, "transaction_type") : NULL;
    double      amount = args ? db_get_num(args, "amount") : 0.0;
    int64_t     asset_id = args ? db_get_int(args, "asset_id") : 0;
    int64_t     linked_id = args ? db_get_int(args, "linked_asset_id") : 0;
    double      quantity = args ? db_get_num(args, "quantity") : 0.0;
    double      price = args ? db_get_num(args, "price") : 0.0;
    double      fee = args ? db_get_num(args, "fee") : 0.0;
    const char* date = args ? csilk_json_get_string(args, "date") : NULL;
    const char* note = args ? csilk_json_get_string(args, "note") : "";

    char date_buf[32];
    if (!date || !date[0]) {
        ai_tool_today_str(date_buf, sizeof(date_buf));
        date = date_buf;
    }

    int64_t arg_lid = args ? db_get_int(args, "ledger_id") : 0;
    int64_t meta_lid = (ctx && ctx->metadata) ? db_get_int(ctx->metadata, "ledger_id") : 0;
    bool    meta_explicit =
        (ctx && ctx->metadata) ? csilk_json_get_bool(ctx->metadata, "ledger_id_explicit") : false;
    int64_t explicit_lid = arg_lid > 0 ? arg_lid : (meta_explicit ? meta_lid : 0);

    int64_t acct_lid = 0, acct_id = 0;
    if (asset_get_ledger_and_account(ctx->pool, asset_id, &acct_lid, &acct_id) != 0) {
        return ai_tool_json_error("asset not found", 1003);
    }
    if (explicit_lid > 0 && acct_lid > 0 && explicit_lid != acct_lid) {
        return ai_tool_json_error("Forbidden: 账本与资产所属账本不一致", 1004);
    }

    int64_t ledger_id = acct_lid > 0 ? acct_lid : (arg_lid > 0 ? arg_lid : meta_lid);
    if (ledger_id <= 0) {
        ledger_id = ledger_get_default((csilk_db_pool_t*)ctx->pool, ctx->user_id);
    }

    char eff_cur[16] = "CNY";
    asset_get_currency_for_user(ctx->pool, ctx->user_id, asset_id, eff_cur, sizeof(eff_cur));

    create_tx_cmd_t cmd = {
        .user_id = ctx->user_id,
        .ledger_id = ledger_id,
        .asset_id = asset_id,
        .linked_asset_id = linked_id,
        .category_id = 0,
        .type = tx_type,
        .amount = amount,
        .price = price,
        .quantity = quantity,
        .fee = fee,
        .currency = eff_cur,
        .note = (note && note[0]) ? note : "AI 交易确认",
        .date = date,
        .source_type = NULL,
    };
    tx_usecase_result_t tres = {0};
    if (tx_usecase_create(ctx->pool, &cmd, &tres) != 0 || tres.code != 0) {
        return ai_tool_json_error(tres.message[0] ? tres.message : "创建交易失败",
                                  tres.code ? tres.code : 1002);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "success", true);
    csilk_json_add_number(res, "id", (double)tres.created_id);
    csilk_json_add_string(res, "message", "交易已入库，资产持仓与关联资金余额已同步更新");
    return ai_tool_json_serialize_free(res);
}

static const ai_tool_spec_t s_transaction_tool_specs[] = {
    {
        .name = "get_transactions",
        .description = "查询当前用户的交易明细流水，支持按交易类型、日期与分页筛选",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_transactions,
        .param_count = 3,
        .params = {
            AI_TOOL_PARAM("transaction_type",
                          "string",
                          "交易类型：income, expense, transfer_in, transfer_out, buy, sell, dividend"),
            AI_TOOL_PARAM("page", "integer", "当前页码，默认 1"),
            AI_TOOL_PARAM("page_size", "integer", "每页条数，默认 20"),
        },
    },
    {
        .name = "get_expense_trend",
        .description = "查询过去若干月的月度收支走势、储蓄结余与月均消费基准",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_expense_trend,
        .param_count = 1,
        .params = {
            AI_TOOL_PARAM("months", "integer", "查询过去 N 个月的收支走势，默认 6"),
        },
    },
    {
        .name = "propose_transaction",
        .description = "拟录入投资或资产交易（买入/卖出/分红/利息/费用），自动匹配标的资产与关联资金"
                       "账户并生成防篡改确认草案；成功后须输出 ```action 卡片（action_type: "
                       "transaction）由用户确认后入库",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_MEDIUM,
        .is_mutation = true,
        .requires_id = true,
        .execute = exec_propose_transaction,
        .param_count = 11,
        .params = {
            AI_TOOL_PARAM_ENUM("transaction_type",
                               "交易类型：buy, sell, dividend, interest, fee, income, expense",
                               true,
                               "buy", "sell", "dividend", "interest", "fee", "income", "expense"),
            AI_TOOL_PARAM_REQ("amount", "number", "交易总金额（必须大于 0）"),
            AI_TOOL_PARAM("asset_id", "integer", "目标资产 ID（与 asset_name 二选一）"),
            AI_TOOL_PARAM("asset_name", "string", "目标资产名称（如 沪深300ETF、招商银行）"),
            AI_TOOL_PARAM("linked_asset_id", "integer", "可选关联扣款/收款资金账户 ID"),
            AI_TOOL_PARAM("linked_asset_name", "string", "可选关联扣款/收款资金账户名称"),
            AI_TOOL_PARAM("quantity", "number", "成交份额/股数（买卖投资标的时建议填写）"),
            AI_TOOL_PARAM("price", "number", "成交单价（不填时若填了 quantity 将自动按 amount/quantity 计算）"),
            AI_TOOL_PARAM("fee", "number", "手续费金额，默认 0"),
            AI_TOOL_PARAM("date", "string", "交易日期 YYYY-MM-DD，默认今天"),
            AI_TOOL_PARAM("note", "string", "交易备注"),
        },
    },
    {
        .name = "confirm_proposed_transaction",
        .description = "用户确认执行投资/资产交易，校验确认令牌后原子入库并更新持仓与余额",
        .action_type = "transaction",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_HIGH,
        .is_mutation = true,
        .requires_id = true,
        .internal_only = true,
        .execute = exec_confirm_proposed_transaction,
        .param_count = 10,
        .params = {
            AI_TOOL_PARAM_REQ("transaction_type", "string", "交易类型"),
            AI_TOOL_PARAM_REQ("amount", "number", "交易金额"),
            AI_TOOL_PARAM_REQ("asset_id", "integer", "目标资产 ID"),
            AI_TOOL_PARAM("linked_asset_id", "integer", "关联资金账户 ID"),
            AI_TOOL_PARAM("quantity", "number", "份额"),
            AI_TOOL_PARAM("price", "number", "单价"),
            AI_TOOL_PARAM("fee", "number", "手续费"),
            AI_TOOL_PARAM("date", "string", "交易日期"),
            AI_TOOL_PARAM("note", "string", "备注"),
            AI_TOOL_PARAM_REQ("draft_token", "string", "防伪确认令牌"),
        },
    },
};

void
ai_tool_transaction_register_all(void)
{
    ai_tool_register_specs(s_transaction_tool_specs,
                           sizeof(s_transaction_tool_specs) / sizeof(s_transaction_tool_specs[0]));
}
