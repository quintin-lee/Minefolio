#include "services/ai/tools/asset_tool.h"
#include "services/ai/tools/schema.h"
#include "repositories/asset_repo.h"
#include "repositories/price_history_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char*
exec_get_assets(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    const char* type = args ? csilk_json_get_string(args, "asset_type") : NULL;
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
    csilk_json_t* list = asset_list(ctx->pool, ctx->user_id, page, page_size, type, &total);

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

    csilk_json_t* cur = asset_get(ctx->pool, ctx->user_id, id);
    if (!cur || csilk_json_array_size(cur) == 0) {
        if (cur) {
            csilk_json_free(cur);
        }
        return strdup("{\"error\":\"asset not found\"}");
    }
    csilk_json_t* detail = csilk_json_copy((csilk_json_t*)csilk_json_array_get(cur, 0));
    csilk_json_free(cur);

    csilk_json_t* ph = price_history_list_by_asset(ctx->pool, ctx->user_id, id, 10);

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
    csilk_json_t* list = asset_list(ctx->pool, ctx->user_id, 1, 200, NULL, &total);

    double        total_assets = 0.0;
    double        total_liabilities = 0.0;
    csilk_json_t* breakdown = csilk_json_array();

    if (list && csilk_json_is_array(list)) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   type = csilk_json_get_string(a, "asset_type") ?: "";
            const char*   name = csilk_json_get_string(a, "name") ?: "";
            double        val = db_get_num(a, "current_value");
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

void
ai_tool_asset_register_all(void)
{
    /* 1. get_assets */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s,
                           "asset_type",
                           "string",
                           "可选资产分类筛选：cash, bank, stock, fund, crypto, loan, credit_card");
        ai_schema_add_prop(s, "page", "integer", "当前页码，默认 1");
        ai_schema_add_prop(s, "page_size", "integer", "每页条数，默认 20");

        ai_tool_t t = {
            .name = "get_assets",
            .description = "查询当前用户的资产与负债账户列表，支持按资产类型和分页筛选",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_get_assets,
        };
        ai_tool_register(&t);
    }

    /* 2. get_asset_detail */
    {
        csilk_json_t* s = ai_schema_create_object();
        ai_schema_add_prop(s, "asset_id", "integer", "要查询详情的资产 ID");
        ai_schema_add_required(s, "asset_id");

        ai_tool_t t = {
            .name = "get_asset_detail",
            .description = "查询指定资产的完整详情、持仓成本与近期净值/价格变动历史",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_get_asset_detail,
        };
        ai_tool_register(&t);
    }

    /* 3. get_asset_breakdown */
    {
        csilk_json_t* s = ai_schema_create_object();

        ai_tool_t t = {
            .name = "get_asset_breakdown",
            .description = "获取用户的资产配置分布、负债构成与总净资产测算",
            .parameters_schema = s,
            .permission = AI_PERM_READ,
            .risk = AI_RISK_LOW,
            .is_mutation = false,
            .validate = NULL,
            .execute = exec_get_asset_breakdown,
        };
        ai_tool_register(&t);
    }
}
