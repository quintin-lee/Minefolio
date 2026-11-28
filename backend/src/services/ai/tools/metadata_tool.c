#include "services/ai/tools/metadata_tool.h"
#include "services/ai/tools/tool_spec.h"
#include "services/ai/tools/tool_helpers.h"
#include "application/account/usecases.h"
#include "application/category/usecases.h"
#include "application/daily_expense/usecases.h"
#include "application/ledger/usecases.h"
#include "application/tag/usecases.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
copy_str_field(csilk_json_t* dst, const csilk_json_t* src, const char* key)
{
    const char* v = csilk_json_get_string(src, key);
    if (v) {
        csilk_json_add_string(dst, key, v);
    }
}

static csilk_json_t*
project_ledger_public(const csilk_json_t* src)
{
    if (!src || !csilk_json_is_object(src)) {
        return NULL;
    }
    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_number(obj, "id", (double)db_get_int(src, "id"));
    copy_str_field(obj, src, "name");
    copy_str_field(obj, src, "description");
    copy_str_field(obj, src, "currency");
    copy_str_field(obj, src, "icon");
    copy_str_field(obj, src, "color");
    csilk_json_add_bool(obj, "is_default", csilk_json_get_bool(src, "is_default"));
    copy_str_field(obj, src, "my_role");
    copy_str_field(obj, src, "owner_username");
    if (csilk_json_get(src, "member_count")) {
        csilk_json_add_number(obj, "member_count", (double)db_get_int(src, "member_count"));
    }
    if (csilk_json_get(src, "total_assets")) {
        csilk_json_add_number(obj, "total_assets", db_get_num(src, "total_assets"));
    }
    copy_str_field(obj, src, "created_at");
    copy_str_field(obj, src, "updated_at");
    return obj;
}

static char*
exec_get_categories(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    const char* type = args ? csilk_json_get_string(args, "type") : NULL;
    if (type && !type[0]) {
        type = NULL;
    }
    int64_t parent_id = args ? db_get_int(args, "parent_id") : 0;
    int64_t explicit_lid = ai_tool_resolve_ledger_id(ctx, args, false);
    int64_t ledger_id = explicit_lid > 0 ? explicit_lid : ai_tool_resolve_ledger_id(ctx, args, true);

    csilk_json_t* list = NULL;
    int           rc = 0;
    if (parent_id > 0) {
        rc = category_usecase_children(ctx->pool, ctx->user_id, ledger_id, parent_id, &list);
    } else {
        rc = category_usecase_list(ctx->pool, ctx->user_id, ledger_id, type, &list);
        if ((rc != 0 || !list || csilk_json_array_size(list) == 0) && explicit_lid <= 0) {
            if (list) {
                csilk_json_free(list);
                list = NULL;
            }
            list = mf_ai_repo_category_list(ctx->pool, ctx->user_id, type);
            rc = 0;
        }
    }

    if (rc != 0) {
        if (list) {
            csilk_json_free(list);
        }
        return ai_tool_json_error("failed to query categories", 0);
    }

    csilk_json_t* arr = list ? list : csilk_json_array();
    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "ledger_id", (double)ledger_id);
    csilk_json_add_number(res, "total", (double)csilk_json_array_size(arr));
    csilk_json_add_array(res, "categories", arr);
    return ai_tool_json_serialize_free(res);
}

static char*
exec_get_tags(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    const char* prefix = args ? csilk_json_get_string(args, "prefix") : NULL;
    int64_t     page = 1, page_size = 50;
    ai_tool_parse_pagination(args, 50, 200, &page, &page_size);

    csilk_json_t* list = NULL;
    int64_t       total = 0;
    int           rc = 0;
    if (prefix && prefix[0]) {
        rc = tag_usecase_suggestions(ctx->pool, ctx->user_id, prefix, &list);
        total = list ? (int64_t)csilk_json_array_size(list) : 0;
    } else {
        rc = tag_usecase_list_paged(ctx->pool, ctx->user_id, page, page_size, &list, &total);
    }

    if (rc != 0) {
        if (list) {
            csilk_json_free(list);
        }
        return ai_tool_json_error("failed to query tags", 0);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total", (double)total);
    csilk_json_add_number(res, "page", (double)page);
    csilk_json_add_number(res, "page_size", (double)page_size);
    csilk_json_add_array(res, "tags", list ? list : csilk_json_array());
    return ai_tool_json_serialize_free(res);
}

static char*
exec_get_expense_by_tag(const ai_tool_t*         tool,
                        const ai_tool_context_t* ctx,
                        const csilk_json_t*      args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    int year = 0, month = 0;
    const char* m_in = args ? csilk_json_get_string(args, "month") : NULL;
    if (m_in && m_in[0]) {
        if (sscanf(m_in, "%d-%d", &year, &month) != 2 || year < 1900 || month < 1 || month > 12) {
            return ai_tool_json_error("invalid month format, expected YYYY-MM", 1002);
        }
    } else {
        time_t    now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        year = tm_now.tm_year + 1900;
        month = tm_now.tm_mon + 1;
    }

    int64_t                        ledger_id = ai_tool_resolve_ledger_id(ctx, args, false);
    daily_expense_monthly_result_t mres = {0};
    csilk_json_t*                  by_cat = NULL;
    csilk_json_t*                  by_tag = NULL;
    csilk_json_t*                  daily = NULL;

    int rc = daily_expense_usecase_monthly(
        ctx->pool, ctx->user_id, ledger_id, year, month, &mres, &by_cat, &by_tag, &daily);
    if (by_cat) {
        csilk_json_free(by_cat);
    }
    if (daily) {
        csilk_json_free(daily);
    }
    if (rc != 0) {
        if (by_tag) {
            csilk_json_free(by_tag);
        }
        return ai_tool_json_error(mres.message[0] ? mres.message : "failed to aggregate by tag",
                                  mres.code);
    }

    char month_buf[16];
    snprintf(month_buf, sizeof(month_buf), "%04d-%02d", year, month);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "month", month_buf);
    csilk_json_add_number(res, "ledger_id", (double)ledger_id);
    csilk_json_add_number(res, "total_income", money_to_double(mres.total_income));
    csilk_json_add_number(res, "total_expense", money_to_double(mres.total_expense));
    csilk_json_add_number(res, "balance", money_to_double(mres.balance));
    csilk_json_add_array(res, "tags", by_tag ? by_tag : csilk_json_array());
    return ai_tool_json_serialize_free(res);
}

static char*
exec_get_ledgers(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    int64_t page = 1, page_size = 20;
    ai_tool_parse_pagination(args, 20, 100, &page, &page_size);

    int64_t       total = 0;
    csilk_json_t* raw =
        ledger_usecase_list_paged(ctx->pool, ctx->user_id, page, page_size, &total);
    csilk_json_t* safe_list = csilk_json_array();
    if (raw && csilk_json_is_array(raw)) {
        size_t n = csilk_json_array_size(raw);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* pub = project_ledger_public(csilk_json_array_get(raw, i));
            if (pub) {
                csilk_json_array_append(safe_list, pub);
            }
        }
    }
    if (raw) {
        csilk_json_free(raw);
    }

    int64_t active_lid = ai_tool_resolve_ledger_id(ctx, NULL, true);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "active_ledger_id", (double)active_lid);
    csilk_json_add_number(res, "total", (double)total);
    csilk_json_add_number(res, "page", (double)page);
    csilk_json_add_number(res, "page_size", (double)page_size);
    csilk_json_add_array(res, "ledgers", safe_list);
    return ai_tool_json_serialize_free(res);
}

static char*
exec_get_ledger_detail(const ai_tool_t*         tool,
                       const ai_tool_context_t* ctx,
                       const csilk_json_t*      args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    int64_t ledger_id = ai_tool_resolve_ledger_id(ctx, args, true);
    if (ledger_id <= 0) {
        return ai_tool_json_error("missing or invalid ledger_id", 1002);
    }

    bool include_members = true;
    if (args && csilk_json_get(args, "include_members")) {
        include_members = csilk_json_get_bool(args, "include_members");
    }

    ledger_usecase_result_t lres = {0};
    csilk_json_t*           raw = ledger_usecase_get(ctx->pool, ctx->user_id, ledger_id, &lres);
    if (!raw) {
        return ai_tool_json_error(lres.message[0] ? lres.message : "ledger not found", lres.code);
    }
    csilk_json_t* pub_ledger = project_ledger_public(raw);
    csilk_json_free(raw);

    char my_role[32] = {0};
    if (ledger_get_user_role((csilk_db_pool_t*)ctx->pool, ledger_id, ctx->user_id, my_role,
                             sizeof(my_role)) &&
        pub_ledger && !csilk_json_get(pub_ledger, "my_role")) {
        csilk_json_add_string(pub_ledger, "my_role", my_role);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_object(res, "ledger", pub_ledger ? pub_ledger : csilk_json_object());

    if (include_members) {
        ledger_usecase_result_t mres = {0};
        csilk_json_t*           raw_members =
            ledger_usecase_list_members(ctx->pool, ctx->user_id, ledger_id, &mres);
        csilk_json_t* safe_members = csilk_json_array();
        if (raw_members && csilk_json_is_array(raw_members)) {
            size_t mn = csilk_json_array_size(raw_members);
            for (size_t i = 0; i < mn; i++) {
                const csilk_json_t* m = csilk_json_array_get(raw_members, i);
                csilk_json_t*       sm = csilk_json_object();
                copy_str_field(sm, m, "username");
                copy_str_field(sm, m, "role");
                copy_str_field(sm, m, "joined_at");
                csilk_json_array_append(safe_members, sm);
            }
        }
        if (raw_members) {
            csilk_json_free(raw_members);
        }
        csilk_json_add_number(res, "member_count", (double)csilk_json_array_size(safe_members));
        csilk_json_add_array(res, "members", safe_members);
    }

    return ai_tool_json_serialize_free(res);
}

static char*
exec_get_accounts(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    int64_t ledger_id = ai_tool_resolve_ledger_id(ctx, args, true);
    int     include_archived = (args && csilk_json_get_bool(args, "include_archived")) ? 1 : 0;
    int64_t page = 1, page_size = 50;
    ai_tool_parse_pagination(args, 50, 200, &page, &page_size);

    csilk_json_t* list = NULL;
    int64_t       total = 0;
    if (account_usecase_query_paged(ctx->pool,
                                    ctx->user_id,
                                    ledger_id,
                                    include_archived,
                                    page,
                                    page_size,
                                    &list,
                                    &total) != 0) {
        if (list) {
            csilk_json_free(list);
        }
        return ai_tool_json_error("failed to query accounts", 0);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "ledger_id", (double)ledger_id);
    csilk_json_add_number(res, "total", (double)total);
    csilk_json_add_number(res, "page", (double)page);
    csilk_json_add_number(res, "page_size", (double)page_size);
    csilk_json_add_array(res, "accounts", list ? list : csilk_json_array());
    return ai_tool_json_serialize_free(res);
}

static char*
exec_propose_create_category(const ai_tool_t*         tool,
                             const ai_tool_context_t* ctx,
                             const csilk_json_t*      args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    const char* name = args ? csilk_json_get_string(args, "name") : NULL;
    if (!name || !name[0]) {
        return ai_tool_json_error("分类名称不能为空", 1002);
    }

    const char* type = args ? csilk_json_get_string(args, "type") : NULL;
    if (!type || !type[0]) {
        type = "expense";
    }
    int64_t     parent_id = args ? db_get_int(args, "parent_id") : 0;
    const char* asset_type = args ? csilk_json_get_string(args, "asset_type") : NULL;
    if (!asset_type || !asset_type[0]) {
        asset_type = "cash";
    }
    const char* currency = args ? csilk_json_get_string(args, "currency") : NULL;
    if (!currency || !currency[0]) {
        currency = "CNY";
    }
    const char* icon = args ? csilk_json_get_string(args, "icon") : NULL;
    int64_t     ledger_id = ai_tool_resolve_ledger_id(ctx, args, true);
    if (ledger_id > 0) {
        char role[32] = {0};
        if (!ledger_get_user_role((csilk_db_pool_t*)ctx->pool, ledger_id, ctx->user_id, role,
                                  sizeof(role)) ||
            (strcmp(role, "owner") != 0 && strcmp(role, "editor") != 0)) {
            return ai_tool_json_error("Forbidden: editor permission required", 1004);
        }
    }

    csilk_json_t* proposed_args = csilk_json_object();
    csilk_json_add_string(proposed_args, "name", name);
    csilk_json_add_string(proposed_args, "type", type);
    csilk_json_add_number(proposed_args, "parent_id", (double)parent_id);
    csilk_json_add_string(proposed_args, "asset_type", asset_type);
    csilk_json_add_string(proposed_args, "currency", currency);
    csilk_json_add_string(proposed_args, "icon", icon ? icon : "");
    csilk_json_add_number(proposed_args, "ledger_id", (double)ledger_id);

    char* out = ai_tool_propose_finalize(ctx,
                                         "create_category",
                                         "confirm_proposed_category",
                                         AI_RISK_MEDIUM,
                                         proposed_args,
                                         NULL);
    csilk_json_free(proposed_args);
    return out;
}

static char*
exec_confirm_proposed_category(const ai_tool_t*         tool,
                               const ai_tool_context_t* ctx,
                               const csilk_json_t*      args)
{
    (void)tool;
    char* verr = ai_tool_confirm_verify(ctx, "confirm_proposed_category", args);
    if (verr) {
        return verr;
    }

    const char* name = args ? csilk_json_get_string(args, "name") : NULL;
    const char* type = args ? csilk_json_get_string(args, "type") : "expense";
    int64_t     parent_id = args ? db_get_int(args, "parent_id") : 0;
    const char* asset_type = args ? csilk_json_get_string(args, "asset_type") : "cash";
    const char* currency = args ? csilk_json_get_string(args, "currency") : "CNY";
    const char* icon = args ? csilk_json_get_string(args, "icon") : "";
    int64_t     ledger_id = ai_tool_resolve_ledger_id(ctx, args, true);

    if (ledger_id > 0) {
        char role[32] = {0};
        if (!ledger_get_user_role((csilk_db_pool_t*)ctx->pool, ledger_id, ctx->user_id, role,
                                  sizeof(role)) ||
            (strcmp(role, "owner") != 0 && strcmp(role, "editor") != 0)) {
            return ai_tool_json_error("Forbidden: editor permission required", 1004);
        }
    }

    create_category_cmd_t cmd = {
        .user_id = ctx->user_id,
        .ledger_id = ledger_id,
        .name = name,
        .parent_id = parent_id,
        .type = (type && type[0]) ? type : "expense",
        .asset_type = (asset_type && asset_type[0]) ? asset_type : "cash",
        .currency = (currency && currency[0]) ? currency : "CNY",
        .icon = icon ? icon : "",
        .sort_order = 0,
    };
    int64_t                   new_id = 0;
    category_usecase_result_t cres = {0};
    int                       rc = category_usecase_create(ctx->pool, &cmd, &new_id, &cres);
    if (rc != 0 || cres.code != 0 || new_id <= 0) {
        return ai_tool_json_error(cres.message[0] ? cres.message : "创建分类失败",
                                  cres.code ? cres.code : 1002);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "success", true);
    csilk_json_add_number(res, "id", (double)new_id);
    csilk_json_add_string(res, "name", name ? name : "");
    csilk_json_add_string(res, "type", cmd.type);
    csilk_json_add_string(res, "message", "分类创建成功");
    return ai_tool_json_serialize_free(res);
}

static char*
exec_propose_create_tag(const ai_tool_t*         tool,
                        const ai_tool_context_t* ctx,
                        const csilk_json_t*      args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    const char* name = args ? csilk_json_get_string(args, "name") : NULL;
    if (!name || !name[0]) {
        return ai_tool_json_error("标签名称不能为空", 1002);
    }
    const char* color = args ? csilk_json_get_string(args, "color") : NULL;
    if (!color || !color[0]) {
        color = "#3b82f6";
    }

    csilk_json_t* proposed_args = csilk_json_object();
    csilk_json_add_string(proposed_args, "name", name);
    csilk_json_add_string(proposed_args, "color", color);

    char* out = ai_tool_propose_finalize(
        ctx, "create_tag", "confirm_proposed_tag", AI_RISK_MEDIUM, proposed_args, NULL);
    csilk_json_free(proposed_args);
    return out;
}

static char*
exec_confirm_proposed_tag(const ai_tool_t*         tool,
                          const ai_tool_context_t* ctx,
                          const csilk_json_t*      args)
{
    (void)tool;
    char* verr = ai_tool_confirm_verify(ctx, "confirm_proposed_tag", args);
    if (verr) {
        return verr;
    }

    const char*      name = args ? csilk_json_get_string(args, "name") : NULL;
    const char*      color = args ? csilk_json_get_string(args, "color") : "#3b82f6";
    create_tag_cmd_t cmd = {
        .user_id = ctx->user_id,
        .name = name,
        .color = (color && color[0]) ? color : "#3b82f6",
    };
    int64_t              new_id = 0;
    tag_usecase_result_t tres = {0};
    int                  rc = tag_usecase_create(ctx->pool, &cmd, &new_id, &tres);
    if (rc != 0 || tres.code != 0 || new_id <= 0) {
        return ai_tool_json_error(tres.message[0] ? tres.message : "创建标签失败（可能已存在同名标签）",
                                  tres.code ? tres.code : 1002);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "success", true);
    csilk_json_add_number(res, "id", (double)new_id);
    csilk_json_add_string(res, "name", name ? name : "");
    csilk_json_add_string(res, "color", cmd.color);
    csilk_json_add_string(res, "message", "标签创建成功");
    return ai_tool_json_serialize_free(res);
}

static const ai_tool_spec_t s_metadata_tool_specs[] = {
    {
        .name = "get_categories",
        .description = "获取当前账本的资产与日常收支分类树，支持按分类类型与父分类筛选",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_categories,
        .param_count = 3,
        .params = {
            AI_TOOL_PARAM_ENUM("type", "分类类型：asset, income, expense, transaction", false,
                               "asset", "income", "expense", "transaction"),
            AI_TOOL_PARAM("ledger_id", "integer", "可选账本 ID，默认使用当前激活账本"),
            AI_TOOL_PARAM("parent_id", "integer", "可选父分类 ID，传值则返回该分类的直接子分类"),
        },
    },
    {
        .name = "get_tags",
        .description = "查询用户的消费标签字典，支持按前缀检索自动补全或分页列出全部标签",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_tags,
        .param_count = 3,
        .params = {
            AI_TOOL_PARAM("prefix", "string", "可选标签名称前缀或关键字筛选"),
            AI_TOOL_PARAM("page", "integer", "当前页码，默认 1"),
            AI_TOOL_PARAM("page_size", "integer", "每页条数，默认 50，最大 200"),
        },
    },
    {
        .name = "get_expense_by_tag",
        .description = "按消费标签汇总统计指定月份的日常支出分布与笔数",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_expense_by_tag,
        .param_count = 2,
        .params = {
            AI_TOOL_PARAM("month", "string", "月份 YYYY-MM，默认当月"),
            AI_TOOL_PARAM("ledger_id", "integer", "可选账本 ID"),
        },
    },
    {
        .name = "get_ledgers",
        .description = "列出当前用户拥有或参与的多账本空间（名称、币种、角色、成员数与资产总额）",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_ledgers,
        .param_count = 2,
        .params = {
            AI_TOOL_PARAM("page", "integer", "当前页码，默认 1"),
            AI_TOOL_PARAM("page_size", "integer", "每页条数，默认 20"),
        },
    },
    {
        .name = "get_ledger_detail",
        .description = "查询指定账本的元信息与成员角色列表（默认当前或默认账本）",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_ledger_detail,
        .param_count = 2,
        .params = {
            AI_TOOL_PARAM("ledger_id", "integer", "可选账本 ID，不传则查询默认账本"),
            AI_TOOL_PARAM("include_members", "boolean", "是否包含账本成员列表，默认 true"),
        },
    },
    {
        .name = "get_accounts",
        .description = "查询当前账本下的资金账户容器（银行卡/微信/券商等）及其汇总金额与权限",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_accounts,
        .param_count = 4,
        .params = {
            AI_TOOL_PARAM("ledger_id", "integer", "可选账本 ID"),
            AI_TOOL_PARAM("include_archived", "boolean", "是否包含已归档账户，默认 false"),
            AI_TOOL_PARAM("page", "integer", "当前页码，默认 1"),
            AI_TOOL_PARAM("page_size", "integer", "每页条数，默认 50"),
        },
    },
    {
        .name = "propose_create_category",
        .description = "拟创建新的收支或资产分类，生成防篡改确认草案；成功后须输出 ```action 卡片"
                       "（action_type: create_category）由用户确认后入库",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_MEDIUM,
        .is_mutation = true,
        .requires_id = true,
        .execute = exec_propose_create_category,
        .param_count = 7,
        .params = {
            AI_TOOL_PARAM_REQ("name", "string", "分类名称"),
            AI_TOOL_PARAM_ENUM("type", "分类类型，默认 expense", false,
                               "expense", "income", "asset", "transaction"),
            AI_TOOL_PARAM("parent_id", "integer", "可选父分类 ID"),
            AI_TOOL_PARAM("asset_type", "string", "资产子类型（type=asset 时有效），默认 cash"),
            AI_TOOL_PARAM("currency", "string", "默认币种，默认 CNY"),
            AI_TOOL_PARAM("icon", "string", "可选图标名称"),
            AI_TOOL_PARAM("ledger_id", "integer", "可选账本 ID"),
        },
    },
    {
        .name = "confirm_proposed_category",
        .description = "用户确认创建分类，校验确认令牌后入库",
        .action_type = "create_category",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_HIGH,
        .is_mutation = true,
        .requires_id = true,
        .internal_only = true,
        .execute = exec_confirm_proposed_category,
        .param_count = 8,
        .params = {
            AI_TOOL_PARAM_REQ("name", "string", "分类名称"),
            AI_TOOL_PARAM("type", "string", "分类类型"),
            AI_TOOL_PARAM("parent_id", "integer", "父分类 ID"),
            AI_TOOL_PARAM("asset_type", "string", "资产子类型"),
            AI_TOOL_PARAM("currency", "string", "币种"),
            AI_TOOL_PARAM("icon", "string", "图标"),
            AI_TOOL_PARAM("ledger_id", "integer", "账本 ID"),
            AI_TOOL_PARAM_REQ("draft_token", "string", "防伪确认令牌"),
        },
    },
    {
        .name = "propose_create_tag",
        .description = "拟创建新的消费标签，生成防篡改确认草案；成功后须输出 ```action 卡片"
                       "（action_type: create_tag）由用户确认后入库",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_MEDIUM,
        .is_mutation = true,
        .requires_id = true,
        .execute = exec_propose_create_tag,
        .param_count = 2,
        .params = {
            AI_TOOL_PARAM_REQ("name", "string", "标签名称"),
            AI_TOOL_PARAM("color", "string", "标签十六进制颜色，如 #3b82f6"),
        },
    },
    {
        .name = "confirm_proposed_tag",
        .description = "用户确认创建标签，校验确认令牌后入库",
        .action_type = "create_tag",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_HIGH,
        .is_mutation = true,
        .requires_id = true,
        .internal_only = true,
        .execute = exec_confirm_proposed_tag,
        .param_count = 3,
        .params = {
            AI_TOOL_PARAM_REQ("name", "string", "标签名称"),
            AI_TOOL_PARAM("color", "string", "标签颜色"),
            AI_TOOL_PARAM_REQ("draft_token", "string", "防伪确认令牌"),
        },
    },
};

void
ai_tool_metadata_register_all(void)
{
    ai_tool_register_specs(s_metadata_tool_specs,
                           sizeof(s_metadata_tool_specs) / sizeof(s_metadata_tool_specs[0]));
}
