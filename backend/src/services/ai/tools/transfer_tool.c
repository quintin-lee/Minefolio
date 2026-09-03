#include "services/ai/tools/transfer_tool.h"
#include "services/ai/tools/schema.h"
#include "services/ai/policy/confirmation.h"
#include "repositories/transfer_repo.h"
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

    int64_t       from_id = 0, to_id = 0;
    char          matched_from_name[128] = "", matched_to_name[128] = "";
    int64_t       total_assets = 0;
    csilk_json_t* assets = asset_list(ctx->pool, ctx->user_id, 1, 100, NULL, &total_assets);
    if (assets && csilk_json_is_array(assets)) {
        size_t asz = csilk_json_array_size(assets);
        for (size_t i = 0; i < asz; i++) {
            csilk_json_t* it = csilk_json_array_get(assets, i);
            const char*   name = csilk_json_get_string(it, "name") ?: "";
            int64_t       id = (int64_t)db_get_num(it, "id");
            if (from_name && from_name[0] && str_icontains(name, from_name) && from_id == 0) {
                from_id = id;
                strncpy(matched_from_name, name, sizeof(matched_from_name) - 1);
            }
            if (to_name && to_name[0] && str_icontains(name, to_name) && to_id == 0) {
                to_id = id;
                strncpy(matched_to_name, name, sizeof(matched_to_name) - 1);
            }
        }
        if (from_id == 0 && asz > 0) {
            from_id = (int64_t)db_get_num(csilk_json_array_get(assets, 0), "id");
            strncpy(matched_from_name,
                    csilk_json_get_string(csilk_json_array_get(assets, 0), "name") ?: "",
                    sizeof(matched_from_name) - 1);
        }
        if (to_id == 0 && asz > 1) {
            to_id = (int64_t)db_get_num(csilk_json_array_get(assets, 1), "id");
            strncpy(matched_to_name,
                    csilk_json_get_string(csilk_json_array_get(assets, 1), "name") ?: "",
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

    char* draft_token = ai_confirmation_create_bound_token(ctx->user_id,
                                                           ctx->session_id,
                                                           "confirm_proposed_transfer",
                                                           proposed_args,
                                                           AI_RISK_HIGH,
                                                           300);
    csilk_json_free(proposed_args);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "propose_success", true);
    csilk_json_add_number(res, "amount", amount);
    csilk_json_add_string(res, "date", date);
    csilk_json_add_number(res, "from_asset_id", (double)from_id);
    csilk_json_add_string(res, "from_asset_name", matched_from_name);
    csilk_json_add_number(res, "to_asset_id", (double)to_id);
    csilk_json_add_string(res, "to_asset_name", matched_to_name);
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
    const char* date = args ? csilk_json_get_string(args, "date") : "";
    const char* note = args ? csilk_json_get_string(args, "note") : "";
    const char* draft_token = args ? csilk_json_get_string(args, "draft_token") : "";

    if (amount <= 0.0) {
        return strdup("{\"error\":\"amount must be positive\"}");
    }
    if (from_id <= 0 || to_id <= 0 || from_id == to_id) {
        return strdup("{\"error\":\"invalid source or target asset\"}");
    }

    ai_confirmation_status_t st = ai_confirmation_verify_and_consume(
        ctx->user_id, ctx->session_id, "confirm_proposed_transfer", args, draft_token);
    if (st != AI_CONFIRM_OK) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}", ai_confirmation_strerror(st));
        return strdup(err_buf);
    }

    csilk_db_exec(ctx->pool, "BEGIN TRANSACTION");

    int64_t transfer_id = transfer_insert(
        ctx->pool, ctx->user_id, from_id, to_id, amount, "CNY", date, note ? note : "");
    if (transfer_id <= 0) {
        csilk_db_exec(ctx->pool, "ROLLBACK");
        return strdup("{\"error\":\"failed to insert transfer record\"}");
    }

    int rc1 = balance_apply_delta(
        ctx->pool, from_id, ctx->user_id, -amount, "transfer", transfer_id, "AI 转账出");
    int rc2 = balance_apply_delta(
        ctx->pool, to_id, ctx->user_id, amount, "transfer", transfer_id, "AI 转账入");

    if (rc1 != 0 || rc2 != 0) {
        csilk_db_exec(ctx->pool, "ROLLBACK");
        return strdup("{\"error\":\"failed to apply balance deltas for transfer\"}");
    }

    csilk_db_exec(ctx->pool, "COMMIT");

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "success", true);
    csilk_json_add_number(res, "id", (double)transfer_id);
    csilk_json_add_string(res, "message", "转账成功，双方账户余额已同步扣增");

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

void
ai_tool_transfer_register_all(void)
{
    /* 1. propose_transfer */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "amount", "number", "转账金额");
        ai_schema_add_prop(s, "from_asset_name", "string", "转出账户名称（如 招商银行卡）");
        ai_schema_add_prop(s, "to_asset_name", "string", "转入账户名称（如 微信零钱）");
        ai_schema_add_prop(s, "date", "string", "转账日期 YYYY-MM-DD");
        ai_schema_add_prop(s, "note", "string", "转账备注");
        ai_schema_add_required(s, "amount");

        ai_tool_t t = {
            .name = "propose_transfer",
            .description =
                "账户间转账智能拟录入草稿，自动匹配转出与转入资产账户，生成待确认卡片数据",
            .parameters_schema = s,
            .permission = AI_PERM_WRITE,
            .risk = AI_RISK_HIGH,
            .is_mutation = true,
            .validate = NULL,
            .execute = exec_propose_transfer,
        };
        ai_tool_register(&t);
    }

    /* 2. confirm_proposed_transfer */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "amount", "number", "转账金额");
        ai_schema_add_prop(s, "from_asset_id", "integer", "转出账户 ID");
        ai_schema_add_prop(s, "to_asset_id", "integer", "转入账户 ID");
        ai_schema_add_prop(s, "date", "string", "转账日期 YYYY-MM-DD");
        ai_schema_add_prop(s, "note", "string", "备注说明");
        ai_schema_add_prop(s, "draft_token", "string", "防伪确认令牌");
        ai_schema_add_required(s, "amount");
        ai_schema_add_required(s, "from_asset_id");
        ai_schema_add_required(s, "to_asset_id");
        ai_schema_add_required(s, "draft_token");

        ai_tool_t t = {
            .name = "confirm_proposed_transfer",
            .description = "确认执行账户间转账，校验确认令牌并原子入库与同步增减双方账户余额",
            .parameters_schema = s,
            .permission = AI_PERM_SENSITIVE,
            .risk = AI_RISK_CRITICAL,
            .is_mutation = true,
            .validate = NULL,
            .execute = exec_confirm_proposed_transfer,
        };
        ai_tool_register(&t);
    }
}
