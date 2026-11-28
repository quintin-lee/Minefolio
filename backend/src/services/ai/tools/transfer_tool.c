#include "services/ai/tools/transfer_tool.h"
#include "services/ai/tools/tool_spec.h"
#include "services/ai/tools/tool_helpers.h"
#include "services/ai/policy/confirmation.h"
#include "application/transfer/usecases.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "common/balance.h"
#include "common/db.h"
#include "core/ledger/ledger_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char*
exec_propose_transfer(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    double      amount = db_get_num(args, "amount");
    const char* from_name = args ? csilk_json_get_string(args, "from_asset_name") : "";
    const char* to_name = args ? csilk_json_get_string(args, "to_asset_name") : "";
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

    int64_t       from_id = 0, to_id = 0;
    char          matched_from_name[128] = "", matched_to_name[128] = "";
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
            if (from_name && from_name[0] && ai_tool_str_icontains(name, from_name) &&
                from_id == 0) {
                from_id = id;
                strncpy(matched_from_name, name, sizeof(matched_from_name) - 1);
            }
            if (to_name && to_name[0] && ai_tool_str_icontains(name, to_name) && to_id == 0) {
                to_id = id;
                strncpy(matched_to_name, name, sizeof(matched_to_name) - 1);
            }
        }
        if (from_id == 0 && asz > 0) {
            from_id = (int64_t)db_get_num(csilk_json_array_get(assets, 0), "id");
            strncpy(matched_from_name,
                    csilk_json_get_string(csilk_json_array_get(assets, 0), "name")
                        ? csilk_json_get_string(csilk_json_array_get(assets, 0), "name")
                        : "",
                    sizeof(matched_from_name) - 1);
        }
        if (to_id == 0 && asz > 1) {
            to_id = (int64_t)db_get_num(csilk_json_array_get(assets, 1), "id");
            strncpy(matched_to_name,
                    csilk_json_get_string(csilk_json_array_get(assets, 1), "name")
                        ? csilk_json_get_string(csilk_json_array_get(assets, 1), "name")
                        : "",
                    sizeof(matched_to_name) - 1);
        }
        csilk_json_free(assets);
    }

    csilk_json_t* proposed_args = csilk_json_object();
    csilk_json_add_number(proposed_args, "amount", amount);
    csilk_json_add_number(proposed_args, "from_asset_id", (double)from_id);
    csilk_json_add_number(proposed_args, "to_asset_id", (double)to_id);
    csilk_json_add_string(proposed_args, "date", date);
    if (note && note[0]) {
        csilk_json_add_string(proposed_args, "note", note);
    }

    csilk_json_t* extra = csilk_json_object();
    csilk_json_add_string(extra, "from_asset_name", matched_from_name);
    csilk_json_add_string(extra, "to_asset_name", matched_to_name);
    if (!note || !note[0]) {
        csilk_json_add_string(extra, "note", "");
    }

    char* out = ai_tool_propose_finalize(ctx,
                                         "transfer",
                                         "confirm_proposed_transfer",
                                         AI_RISK_HIGH,
                                         proposed_args,
                                         extra);
    csilk_json_free(proposed_args);
    csilk_json_free(extra);
    return out;
}

static char*
exec_confirm_proposed_transfer(const ai_tool_t*         tool,
                               const ai_tool_context_t* ctx,
                               const csilk_json_t*      args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    double      amount = db_get_num(args, "amount");
    int64_t     from_id = (int64_t)db_get_num(args, "from_asset_id");
    int64_t     to_id = (int64_t)db_get_num(args, "to_asset_id");
    const char* note = args ? csilk_json_get_string(args, "note") : "";
    const char* date = args ? csilk_json_get_string(args, "date") : NULL;
    const char* currency = args ? csilk_json_get_string(args, "currency") : NULL;

    if (amount <= 0.0) {
        return strdup("{\"error\":\"amount must be positive\"}");
    }
    if (from_id <= 0 || to_id <= 0 || from_id == to_id) {
        return strdup("{\"error\":\"invalid source or target asset\"}");
    }

    char* verr = ai_tool_confirm_verify(ctx, "confirm_proposed_transfer", args);
    if (verr) {
        return verr;
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

    int64_t arg_lid = args ? db_get_int(args, "ledger_id") : 0;
    int64_t meta_lid = (ctx && ctx->metadata) ? db_get_int(ctx->metadata, "ledger_id") : 0;
    bool    meta_explicit =
        (ctx && ctx->metadata) ? csilk_json_get_bool(ctx->metadata, "ledger_id_explicit") : false;
    int64_t explicit_lid = arg_lid > 0 ? arg_lid : (meta_explicit ? meta_lid : 0);

    int64_t from_lid = 0, to_lid = 0;
    if (asset_get_ledger_and_account(ctx->pool, from_id, &from_lid, NULL) == 0 &&
        asset_get_ledger_and_account(ctx->pool, to_id, &to_lid, NULL) == 0) {
        if (explicit_lid > 0 && ((from_lid > 0 && explicit_lid != from_lid) ||
                                 (to_lid > 0 && explicit_lid != to_lid))) {
            return strdup("{\"error\":\"Forbidden: 不支持跨账本划转\",\"code\":1004}");
        }
    }
    int64_t ledger_id =
        (from_lid > 0) ? from_lid : ((to_lid > 0) ? to_lid : (arg_lid > 0 ? arg_lid : meta_lid));

    create_transfer_cmd_t cmd = {
        .user_id = ctx->user_id,
        .from_asset_id = from_id,
        .to_asset_id = to_id,
        .amount = amount,
        .currency = (currency && currency[0]) ? currency : "CNY",
        .transfer_date = date,
        .note = (note && note[0]) ? note : "AI 转账",
        .ledger_id = ledger_id,
    };
    transfer_usecase_result_t tres = {0};
    if (transfer_usecase_create(ctx->pool, &cmd, &tres) != 0 || tres.code != 0) {
        csilk_json_t* err_obj = csilk_json_object();
        csilk_json_add_string(
            err_obj, "error", tres.message[0] ? tres.message : "failed to create transfer");
        csilk_json_add_int(err_obj, "code", tres.code ? tres.code : 500);
        size_t elen = 0;
        char*  es = csilk_json_serialize(err_obj, &elen);
        csilk_json_free(err_obj);
        return es;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "success", true);
    csilk_json_add_number(res, "id", (double)tres.id);
    csilk_json_add_string(res, "message", "转账成功，双方账户余额已同步扣增");

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static const ai_tool_spec_t s_transfer_tool_specs[] = {
    {
        .name = "propose_transfer",
        .description = "账户间转账智能拟录入草稿，自动匹配转出与转入资产账户，生成待确认卡片数据。"
                       "成功后必须将返回的 draft_token/from_asset_id/to_asset_id/"
                       "amount/date/note 填入 ```action 卡片（action_type: transfer）展示给用户",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_HIGH,
        .is_mutation = true,
        .requires_id = true,
        .execute = exec_propose_transfer,
        .param_count = 5,
        .params = {
            AI_TOOL_PARAM_REQ("amount", "number", "转账金额"),
            AI_TOOL_PARAM("from_asset_name", "string", "转出账户名称（如 招商银行卡）"),
            AI_TOOL_PARAM("to_asset_name", "string", "转入账户名称（如 微信零钱）"),
            AI_TOOL_PARAM("date", "string", "转账日期 YYYY-MM-DD"),
            AI_TOOL_PARAM("note", "string", "转账备注"),
        },
    },
    {
        .name = "confirm_proposed_transfer",
        .description = "确认执行账户间转账，校验确认令牌并原子入库与同步增减双方账户余额",
        .action_type = "transfer",
        .permission = AI_PERM_SENSITIVE,
        .risk = AI_RISK_CRITICAL,
        .is_mutation = true,
        .requires_id = true,
        .internal_only = true,
        .execute = exec_confirm_proposed_transfer,
        .param_count = 6,
        .params = {
            AI_TOOL_PARAM_REQ("amount", "number", "转账金额"),
            AI_TOOL_PARAM_REQ("from_asset_id", "integer", "转出账户 ID"),
            AI_TOOL_PARAM_REQ("to_asset_id", "integer", "转入账户 ID"),
            AI_TOOL_PARAM("date", "string", "转账日期 YYYY-MM-DD"),
            AI_TOOL_PARAM("note", "string", "备注说明"),
            AI_TOOL_PARAM_REQ("draft_token", "string", "防伪确认令牌"),
        },
    },
};

void
ai_tool_transfer_register_all(void)
{
    ai_tool_register_specs(s_transfer_tool_specs,
                           sizeof(s_transfer_tool_specs) / sizeof(s_transfer_tool_specs[0]));
}
