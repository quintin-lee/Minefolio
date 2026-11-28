#include "services/ai/tools/asset_tool.h"
#include "services/ai/tools/tool_spec.h"
#include "services/ai/tools/tool_helpers.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char*
exec_get_assets(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    const char* type = args ? csilk_json_get_string(args, "asset_type") : NULL;
    int64_t     page = 1, page_size = 20;
    ai_tool_parse_pagination(args, 20, 100, &page, &page_size);

    int64_t total = 0;
    int64_t account_id = 0;
    if (args) {
        double ac = db_get_num(args, "account_id");
        if (ac >= 1.0) {
            account_id = (int64_t)ac;
        }
    }
    int64_t ledger_id = 0;
    if (ctx && ctx->metadata) {
        ledger_id = db_get_int(ctx->metadata, "ledger_id");
    }
    csilk_json_t* list = asset_list((csilk_db_pool_t*)ctx->pool,
                                    ctx->user_id,
                                    0,
                                    ledger_id,
                                    page,
                                    page_size,
                                    type,
                                    account_id,
                                    &total);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total", (double)total);
    csilk_json_add_number(res, "page", (double)page);
    csilk_json_add_number(res, "page_size", (double)page_size);
    if (list) {
        csilk_json_add_array(res, "assets", list);
    } else {
        csilk_json_add_array(res, "assets", csilk_json_array());
    }

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_get_asset_detail(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    int64_t id = (int64_t)db_get_num(args, "asset_id");
    if (id <= 0) {
        id = (int64_t)db_get_num(args, "id");
    }
    if (id <= 0) {
        return strdup("{\"error\":\"missing or invalid asset_id\"}");
    }

    csilk_json_t* cur = mf_ai_repo_asset_get(ctx->pool, ctx->user_id, id);
    if (!cur || csilk_json_array_size(cur) == 0) {
        if (cur) {
            csilk_json_free(cur);
        }
        return strdup("{\"error\":\"asset not found\"}");
    }
    csilk_json_t* detail = csilk_json_copy((csilk_json_t*)csilk_json_array_get(cur, 0));
    csilk_json_free(cur);

    csilk_json_t* ph = mf_ai_repo_price_history_list(ctx->pool, ctx->user_id, id, 10);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_object(res, "asset", detail);
    if (ph) {
        csilk_json_add_array(res, "price_history", ph);
    }

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
exec_get_asset_breakdown(const ai_tool_t*         tool,
                         const ai_tool_context_t* ctx,
                         const csilk_json_t*      args)
{
    (void)tool;
    (void)args;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    int64_t       total = 0;
    csilk_json_t* list = mf_ai_repo_asset_list(ctx->pool, ctx->user_id, 1, 200, NULL, &total);

    double        total_assets = 0.0;
    double        total_liabilities = 0.0;
    csilk_json_t* breakdown = csilk_json_array();

    if (list && csilk_json_is_array(list)) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   type = csilk_json_get_string(a, "asset_type")
                                     ? csilk_json_get_string(a, "asset_type")
                                     : "";
            const char*   name =
                csilk_json_get_string(a, "name") ? csilk_json_get_string(a, "name") : "";
            double val = db_get_num(a, "current_value");
            if (val == 0.0) {
                val = db_get_num(a, "balance");
            }

            bool is_liab = (strcmp(type, "loan") == 0 || strcmp(type, "credit_card") == 0 ||
                            strcmp(type, "other_liability") == 0);
            if (is_liab) {
                total_liabilities += val;
            } else {
                total_assets += val;
            }

            csilk_json_t* item = csilk_json_object();
            csilk_json_add_string(item, "name", name);
            csilk_json_add_string(item, "type", type);
            csilk_json_add_number(item, "amount", val);
            csilk_json_add_bool(item, "is_liability", is_liab);
            csilk_json_array_append(breakdown, item);
        }
        csilk_json_free(list);
    }

    double net_worth = total_assets - total_liabilities;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total_assets", total_assets);
    csilk_json_add_number(res, "total_liabilities", total_liabilities);
    csilk_json_add_number(res, "net_worth", net_worth);
    csilk_json_add_array(res, "breakdown", breakdown);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static const ai_tool_spec_t s_asset_tool_specs[] = {
    {
        .name = "get_assets",
        .description = "查询当前用户的资产与负债账户列表，支持按资产类型和分页筛选",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_assets,
        .param_count = 4,
        .params = {
            AI_TOOL_PARAM("asset_type",
                          "string",
                          "可选资产分类筛选：cash, bank, stock, fund, crypto, loan, credit_card"),
            AI_TOOL_PARAM("page", "integer", "当前页码，默认 1"),
            AI_TOOL_PARAM("page_size", "integer", "每页条数，默认 20"),
            AI_TOOL_PARAM("account_id", "integer", "可选账户 ID 筛选"),
        },
    },
    {
        .name = "get_asset_detail",
        .description = "查询指定资产的完整详情、持仓成本与近期净值/价格变动历史",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_asset_detail,
        .param_count = 1,
        .params = {
            AI_TOOL_PARAM_REQ("asset_id", "integer", "要查询详情的资产 ID"),
        },
    },
    {
        .name = "get_asset_breakdown",
        .description = "获取用户的资产配置分布、负债构成与总净资产测算",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_asset_breakdown,
    },
};

void
ai_tool_asset_register_all(void)
{
    ai_tool_register_specs(s_asset_tool_specs,
                           sizeof(s_asset_tool_specs) / sizeof(s_asset_tool_specs[0]));
}
