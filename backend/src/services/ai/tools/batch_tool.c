#include "services/ai/tools/batch_tool.h"
#include "services/ai/tools/registry.h"
#include "services/ai/tools/schema.h"
#include "services/ai/policy/confirmation.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "core/ledger/ledger_engine.h"
#include "common/balance.h"
#include "common/db.h"
#include "csilk/csilk.h"
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

static void
get_today_date(char* buf, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    snprintf(buf, sz, "%04d-%02d-%02d", tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);
}

static bool
resolve_asset(const csilk_json_t* assets,
              int64_t             provided_id,
              const char*         provided_name,
              int64_t*            out_id,
              char*               out_name,
              size_t              out_name_sz,
              size_t              fallback_idx)
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
    if (asz == 0) {
        return false;
    }

    /* 1. If provided_id > 0, find exact match */
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

    /* 2. If provided_name is non-empty, find substring match */
    if (provided_name && provided_name[0]) {
        for (size_t i = 0; i < asz; i++) {
            const csilk_json_t* it = csilk_json_array_get(assets, i);
            const char*         n = csilk_json_get_string(it, "name");
            if (n && str_icontains(n, provided_name)) {
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

    /* 3. Fallback to fallback_idx if within bounds */
    if (fallback_idx < asz) {
        const csilk_json_t* it = csilk_json_array_get(assets, fallback_idx);
        if (out_id) {
            *out_id = (int64_t)db_get_num(it, "id");
        }
        const char* n = csilk_json_get_string(it, "name");
        if (out_name && out_name_sz > 0) {
            snprintf(out_name, out_name_sz, "%s", n ? n : "");
        }
        return true;
    }

    return false;
}

static bool
resolve_category(const csilk_json_t* cats,
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
    if (!cats || !csilk_json_is_array(cats)) {
        return false;
    }
    size_t csz = csilk_json_array_size(cats);
    if (csz == 0) {
        return false;
    }

    /* 1. Exact match by ID */
    if (provided_id > 0) {
        for (size_t i = 0; i < csz; i++) {
            const csilk_json_t* it = csilk_json_array_get(cats, i);
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

    /* 2. Substring match by name */
    if (provided_name && provided_name[0]) {
        for (size_t i = 0; i < csz; i++) {
            const csilk_json_t* it = csilk_json_array_get(cats, i);
            const char*         n = csilk_json_get_string(it, "name");
            if (n && str_icontains(n, provided_name)) {
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

    /* 3. Fallback to first category */
    const csilk_json_t* it = csilk_json_array_get(cats, 0);
    if (out_id) {
        *out_id = (int64_t)db_get_num(it, "id");
    }
    const char* n = csilk_json_get_string(it, "name");
    if (out_name && out_name_sz > 0) {
        snprintf(out_name, out_name_sz, "%s", n ? n : "");
    }
    return true;
}

static char*
exec_propose_batch_actions(const ai_tool_t*         tool,
                           const ai_tool_context_t* ctx,
                           const csilk_json_t*      args)
{
    (void)tool;
    if (!ctx || !ctx->pool || ctx->user_id <= 0) {
        return strdup("{\"error\":\"invalid context\"}");
    }

    if (!args) {
        return strdup("{\"error\":\"missing arguments\"}");
    }

    const csilk_json_t* actions_arr = csilk_json_get(args, "actions");
    if (!actions_arr || !csilk_json_is_array(actions_arr)) {
        return strdup("{\"error\":\"actions must be a JSON array\"}");
    }

    size_t num_actions = csilk_json_array_size(actions_arr);
    if (num_actions == 0 || num_actions > 20) {
        return strdup("{\"error\":\"actions count must be between 1 and 20\"}");
    }

    int64_t       total_assets = 0;
    csilk_json_t* assets =
        mf_ai_repo_asset_list(ctx->pool, ctx->user_id, 1, 100, NULL, &total_assets);
    csilk_json_t* expense_cats = mf_ai_repo_category_list(ctx->pool, ctx->user_id, "expense");
    csilk_json_t* income_cats = mf_ai_repo_category_list(ctx->pool, ctx->user_id, "income");

    csilk_json_t* normalized_actions = csilk_json_array();
    char          summary_buf[4096] = {0};
    size_t        summary_len = 0;

    for (size_t i = 0; i < num_actions; i++) {
        const csilk_json_t* act = csilk_json_array_get(actions_arr, i);
        if (!act || !csilk_json_is_object(act)) {
            csilk_json_free(normalized_actions);
            if (assets) {
                csilk_json_free(assets);
            }
            if (expense_cats) {
                csilk_json_free(expense_cats);
            }
            if (income_cats) {
                csilk_json_free(income_cats);
            }
            return strdup("{\"error\":\"each action must be a JSON object\"}");
        }

        const char* action_type = csilk_json_get_string(act, "action_type");
        if (!action_type ||
            (strcmp(action_type, "daily_expense") != 0 && strcmp(action_type, "transfer") != 0)) {
            csilk_json_free(normalized_actions);
            if (assets) {
                csilk_json_free(assets);
            }
            if (expense_cats) {
                csilk_json_free(expense_cats);
            }
            if (income_cats) {
                csilk_json_free(income_cats);
            }
            return strdup("{\"error\":\"action_type must be daily_expense or transfer\"}");
        }

        double amount = db_get_num(act, "amount");
        if (amount <= 0.0) {
            csilk_json_free(normalized_actions);
            if (assets) {
                csilk_json_free(assets);
            }
            if (expense_cats) {
                csilk_json_free(expense_cats);
            }
            if (income_cats) {
                csilk_json_free(income_cats);
            }
            return strdup("{\"error\":\"amount must be positive\"}");
        }

        const char* date = csilk_json_get_string(act, "date");
        char        date_buf[32] = {0};
        if (!date || !date[0]) {
            get_today_date(date_buf, sizeof(date_buf));
            date = date_buf;
        }
        const char* note = csilk_json_get_string(act, "note");
        if (!note) {
            note = "";
        }

        if (strcmp(action_type, "daily_expense") == 0) {
            const char* type = csilk_json_get_string(act, "type");
            if (!type || !type[0] ||
                (strcmp(type, "income") != 0 && strcmp(type, "expense") != 0)) {
                type = "expense";
            }

            int64_t     prov_asset_id = (int64_t)db_get_num(act, "asset_id");
            const char* prov_asset_name = csilk_json_get_string(act, "asset_name");
            int64_t     matched_asset_id = 0;
            char        matched_asset_name[128] = {0};
            if (!resolve_asset(assets,
                               prov_asset_id,
                               prov_asset_name,
                               &matched_asset_id,
                               matched_asset_name,
                               sizeof(matched_asset_name),
                               0) ||
                matched_asset_id <= 0) {
                csilk_json_free(normalized_actions);
                if (assets) {
                    csilk_json_free(assets);
                }
                if (expense_cats) {
                    csilk_json_free(expense_cats);
                }
                if (income_cats) {
                    csilk_json_free(income_cats);
                }
                return strdup("{\"error\":\"failed to resolve asset for daily_expense\"}");
            }

            int64_t             prov_cat_id = (int64_t)db_get_num(act, "category_id");
            const char*         prov_cat_name = csilk_json_get_string(act, "category_name");
            int64_t             matched_cat_id = 0;
            char                matched_cat_name[128] = {0};
            const csilk_json_t* target_cats =
                (strcmp(type, "income") == 0) ? income_cats : expense_cats;
            if (!resolve_category(target_cats,
                                  prov_cat_id,
                                  prov_cat_name,
                                  &matched_cat_id,
                                  matched_cat_name,
                                  sizeof(matched_cat_name)) ||
                matched_cat_id <= 0) {
                csilk_json_free(normalized_actions);
                if (assets) {
                    csilk_json_free(assets);
                }
                if (expense_cats) {
                    csilk_json_free(expense_cats);
                }
                if (income_cats) {
                    csilk_json_free(income_cats);
                }
                return strdup("{\"error\":\"failed to resolve category for daily_expense\"}");
            }

            csilk_json_t* norm_item = csilk_json_object();
            csilk_json_add_string(norm_item, "action_type", "daily_expense");
            csilk_json_add_number(norm_item, "amount", amount);
            csilk_json_add_string(norm_item, "type", type);
            csilk_json_add_number(norm_item, "category_id", (double)matched_cat_id);
            csilk_json_add_string(norm_item, "category_name", matched_cat_name);
            csilk_json_add_number(norm_item, "asset_id", (double)matched_asset_id);
            csilk_json_add_string(norm_item, "asset_name", matched_asset_name);
            csilk_json_add_string(norm_item, "date", date);
            csilk_json_add_string(norm_item, "note", note);
            csilk_json_array_append(normalized_actions, norm_item);

            char        item_summary[256];
            const char* type_desc = (strcmp(type, "income") == 0) ? "收入" : "支出";
            if (note && note[0]) {
                snprintf(item_summary,
                         sizeof(item_summary),
                         "%zu. %s %.2f CNY (%s)",
                         i + 1,
                         type_desc,
                         amount,
                         note);
            } else if (matched_cat_name[0]) {
                snprintf(item_summary,
                         sizeof(item_summary),
                         "%zu. %s %.2f CNY (%s)",
                         i + 1,
                         type_desc,
                         amount,
                         matched_cat_name);
            } else {
                snprintf(item_summary,
                         sizeof(item_summary),
                         "%zu. %s %.2f CNY",
                         i + 1,
                         type_desc,
                         amount);
            }

            if (i > 0 && summary_len + 2 < sizeof(summary_buf)) {
                strcat(summary_buf, "; ");
                summary_len += 2;
            }
            size_t ilen = strlen(item_summary);
            if (summary_len + ilen < sizeof(summary_buf)) {
                strcat(summary_buf, item_summary);
                summary_len += ilen;
            }
        } else if (strcmp(action_type, "transfer") == 0) {
            int64_t     prov_from_id = (int64_t)db_get_num(act, "from_asset_id");
            const char* prov_from_name = csilk_json_get_string(act, "from_asset_name");
            int64_t     from_id = 0;
            char        from_name[128] = {0};
            if (!resolve_asset(assets,
                               prov_from_id,
                               prov_from_name,
                               &from_id,
                               from_name,
                               sizeof(from_name),
                               0) ||
                from_id <= 0) {
                csilk_json_free(normalized_actions);
                if (assets) {
                    csilk_json_free(assets);
                }
                if (expense_cats) {
                    csilk_json_free(expense_cats);
                }
                if (income_cats) {
                    csilk_json_free(income_cats);
                }
                return strdup("{\"error\":\"failed to resolve from_asset for transfer\"}");
            }

            int64_t     prov_to_id = (int64_t)db_get_num(act, "to_asset_id");
            const char* prov_to_name = csilk_json_get_string(act, "to_asset_name");
            int64_t     to_id = 0;
            char        to_name[128] = {0};
            if (!resolve_asset(
                    assets, prov_to_id, prov_to_name, &to_id, to_name, sizeof(to_name), 1) ||
                to_id <= 0) {
                csilk_json_free(normalized_actions);
                if (assets) {
                    csilk_json_free(assets);
                }
                if (expense_cats) {
                    csilk_json_free(expense_cats);
                }
                if (income_cats) {
                    csilk_json_free(income_cats);
                }
                return strdup("{\"error\":\"failed to resolve to_asset for transfer\"}");
            }

            if (from_id == to_id) {
                csilk_json_free(normalized_actions);
                if (assets) {
                    csilk_json_free(assets);
                }
                if (expense_cats) {
                    csilk_json_free(expense_cats);
                }
                if (income_cats) {
                    csilk_json_free(income_cats);
                }
                return strdup("{\"error\":\"from_asset_id and to_asset_id must be distinct\"}");
            }

            csilk_json_t* norm_item = csilk_json_object();
            csilk_json_add_string(norm_item, "action_type", "transfer");
            csilk_json_add_number(norm_item, "amount", amount);
            csilk_json_add_number(norm_item, "from_asset_id", (double)from_id);
            csilk_json_add_string(norm_item, "from_asset_name", from_name);
            csilk_json_add_number(norm_item, "to_asset_id", (double)to_id);
            csilk_json_add_string(norm_item, "to_asset_name", to_name);
            csilk_json_add_string(norm_item, "date", date);
            csilk_json_add_string(norm_item, "note", note);
            csilk_json_array_append(normalized_actions, norm_item);

            char item_summary[256];
            if (note && note[0]) {
                snprintf(item_summary,
                         sizeof(item_summary),
                         "%zu. 转账 %.2f CNY (%s)",
                         i + 1,
                         amount,
                         note);
            } else if (from_name[0] && to_name[0]) {
                snprintf(item_summary,
                         sizeof(item_summary),
                         "%zu. 转账 %.2f CNY (%s -> %s)",
                         i + 1,
                         amount,
                         from_name,
                         to_name);
            } else {
                snprintf(item_summary, sizeof(item_summary), "%zu. 转账 %.2f CNY", i + 1, amount);
            }

            if (i > 0 && summary_len + 2 < sizeof(summary_buf)) {
                strcat(summary_buf, "; ");
                summary_len += 2;
            }
            size_t ilen = strlen(item_summary);
            if (summary_len + ilen < sizeof(summary_buf)) {
                strcat(summary_buf, item_summary);
                summary_len += ilen;
            }
        }
    }

    if (assets) {
        csilk_json_free(assets);
    }
    if (expense_cats) {
        csilk_json_free(expense_cats);
    }
    if (income_cats) {
        csilk_json_free(income_cats);
    }

    char* draft_token = ai_confirmation_create_batch_token(
        ctx->user_id, ctx->session_id, normalized_actions, AI_RISK_HIGH, 300);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "propose_success", true);
    csilk_json_add_bool(res, "requires_confirmation", true);
    csilk_json_add_number(res, "count", (double)num_actions);
    csilk_json_add_string(res, "summary", summary_buf);
    csilk_json_add_string(res, "draft_token", draft_token ? draft_token : "");
    csilk_json_add_array(res, "actions", normalized_actions);

    if (draft_token) {
        free(draft_token);
    }

    size_t out_len = 0;
    char*  out_str = csilk_json_serialize(res, &out_len);
    csilk_json_free(res);
    return out_str;
}

void
ai_tool_batch_register_all(void)
{
    static const char* s_batch_schema_json =
        "{\n"
        "  \"type\": \"object\",\n"
        "  \"properties\": {\n"
        "    \"actions\": {\n"
        "      \"type\": \"array\",\n"
        "      \"description\": \"拟执行的操作列表 (1~20 条)\",\n"
        "      \"items\": {\n"
        "        \"type\": \"object\",\n"
        "        \"properties\": {\n"
        "          \"action_type\": {\"type\": \"string\", \"enum\": [\"daily_expense\", "
        "\"transfer\"]},\n"
        "          \"amount\": {\"type\": \"number\"},\n"
        "          \"type\": {\"type\": \"string\", \"enum\": [\"expense\", \"income\"]},\n"
        "          \"category_id\": {\"type\": \"integer\"},\n"
        "          \"category_name\": {\"type\": \"string\"},\n"
        "          \"asset_id\": {\"type\": \"integer\"},\n"
        "          \"asset_name\": {\"type\": \"string\"},\n"
        "          \"from_asset_id\": {\"type\": \"integer\"},\n"
        "          \"from_asset_name\": {\"type\": \"string\"},\n"
        "          \"to_asset_id\": {\"type\": \"integer\"},\n"
        "          \"to_asset_name\": {\"type\": \"string\"},\n"
        "          \"date\": {\"type\": \"string\"},\n"
        "          \"note\": {\"type\": \"string\"}\n"
        "        },\n"
        "        \"required\": [\"action_type\", \"amount\"]\n"
        "      }\n"
        "    }\n"
        "  },\n"
        "  \"required\": [\"actions\"]\n"
        "}";

    csilk_json_t* s = csilk_json_parse(s_batch_schema_json);

    ai_tool_t t = {
        .name = "propose_batch_actions",
        .description = "批量财务操作拟录入（支持支出、收入及调拨转账），"
                       "验证账户有效性并生成复合防篡改确认草案，返回待确认卡片数据。",
        .parameters_schema = s,
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_MEDIUM,
        .is_mutation = true,
        .requires_id = true,
        .validate = NULL,
        .execute = exec_propose_batch_actions,
    };
    ai_tool_register(&t);
}

int
ai_batch_actions_execute(csilk_db_pool_t*    pool,
                         int64_t             user_id,
                         const csilk_json_t* actions,
                         const char*         draft_token,
                         csilk_json_t**      out_data,
                         int*                out_code,
                         char*               err_buf,
                         size_t              err_len)
{
    if (out_data) {
        *out_data = NULL;
    }
    if (out_code) {
        *out_code = 1002;
    }
    if (err_buf && err_len > 0) {
        err_buf[0] = '\0';
    }

    if (!pool || user_id <= 0 || !actions || !csilk_json_is_array(actions) || !draft_token ||
        !draft_token[0]) {
        if (err_buf && err_len > 0) {
            snprintf(err_buf, err_len, "缺少参数或用户未授权");
        }
        return -1;
    }

    size_t cnt = csilk_json_array_size(actions);
    if (cnt == 0 || cnt > 20) {
        if (err_buf && err_len > 0) {
            snprintf(err_buf, err_len, "动作列表数量必须在 1~20 之间");
        }
        return -1;
    }

    /* 1. 验证并原子消费 draft_token (Anti-replay + HMAC verification) */
    ai_confirmation_status_t st =
        ai_confirmation_verify_and_consume_batch(user_id, 0, actions, draft_token);
    if (st != AI_CONFIRM_OK) {
        if (st == AI_CONFIRM_ERR_REPLAY) {
            if (out_code) {
                *out_code = 1004;
            }
            if (err_buf && err_len > 0) {
                snprintf(err_buf, err_len, "确认令牌已被使用或已失效");
            }
        } else if (st == AI_CONFIRM_ERR_EXPIRED) {
            if (out_code) {
                *out_code = 1002;
            }
            if (err_buf && err_len > 0) {
                snprintf(err_buf, err_len, "确认令牌已过期");
            }
        } else if (st == AI_CONFIRM_ERR_USER_MISMATCH) {
            if (out_code) {
                *out_code = 1004;
            }
            if (err_buf && err_len > 0) {
                snprintf(err_buf, err_len, "用户身份不匹配");
            }
        } else if (st == AI_CONFIRM_ERR_ARGS_MISMATCH) {
            if (out_code) {
                *out_code = 1002;
            }
            if (err_buf && err_len > 0) {
                snprintf(err_buf, err_len, "提交参数与拟执行草案不匹配，禁止执行");
            }
        } else {
            if (out_code) {
                *out_code = 1002;
            }
            if (err_buf && err_len > 0) {
                snprintf(err_buf, err_len, "确认令牌校验失败: %s", ai_confirmation_strerror(st));
            }
        }
        return -1;
    }

    /* 2. 开启原子事务保存点 */
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_confirm_batch", &scope) != 0) {
        if (out_code) {
            *out_code = 500;
        }
        if (err_buf && err_len > 0) {
            snprintf(err_buf, err_len, "数据库事务开启失败");
        }
        return -1;
    }

    char today_buf[32];
    get_today_date(today_buf, sizeof(today_buf));

    csilk_json_t* results_arr = csilk_json_array();
    for (size_t i = 0; i < cnt; i++) {
        const csilk_json_t* it = csilk_json_array_get(actions, i);
        const char*         action_type = csilk_json_get_string(it, "action_type");
        if (!action_type || !action_type[0]) {
            action_type = csilk_json_get_string(it, "type");
        }
        if (!action_type || !action_type[0]) {
            db_tx_scope_rollback(pool, &scope);
            csilk_json_free(results_arr);
            if (out_code) {
                *out_code = 1002;
            }
            if (err_buf && err_len > 0) {
                snprintf(err_buf, err_len, "动作 #%zu 缺少 action_type", i + 1);
            }
            return -1;
        }

        double amount = db_get_num(it, "amount");
        if (amount <= 0.0) {
            db_tx_scope_rollback(pool, &scope);
            csilk_json_free(results_arr);
            if (out_code) {
                *out_code = 1002;
            }
            if (err_buf && err_len > 0) {
                snprintf(err_buf, err_len, "动作 #%zu 金额必须大于 0", i + 1);
            }
            return -1;
        }

        const char* note = csilk_json_get_string(it, "note");
        const char* dt = csilk_json_get_string(it, "date");
        if (!dt || !dt[0]) {
            dt = today_buf;
        }

        if (strcmp(action_type, "daily_expense") == 0) {
            const char* type = csilk_json_get_string(it, "type");
            if (!type || !type[0] || strcmp(type, "daily_expense") == 0) {
                type = "expense";
            }
            int64_t category_id = (int64_t)db_get_num(it, "category_id");
            int64_t asset_id = (int64_t)db_get_num(it, "asset_id");
            if (asset_id <= 0) {
                db_tx_scope_rollback(pool, &scope);
                csilk_json_free(results_arr);
                if (out_code) {
                    *out_code = 1002;
                }
                if (err_buf && err_len > 0) {
                    snprintf(err_buf, err_len, "动作 #%zu 缺少有效 asset_id", i + 1);
                }
                return -1;
            }

            int64_t de_id = mf_ai_repo_daily_expense_insert(
                pool, user_id, dt, type, amount, "CNY", category_id, asset_id, note ? note : "");
            if (de_id <= 0) {
                db_tx_scope_rollback(pool, &scope);
                csilk_json_free(results_arr);
                if (out_code) {
                    *out_code = 500;
                }
                if (err_buf && err_len > 0) {
                    snprintf(err_buf, err_len, "动作 #%zu 写入收支失败", i + 1);
                }
                return -1;
            }

            int     is_inc = (strcmp(type, "income") == 0);
            money_t amt_m;
            money_from_double(amount, CURRENCY_CNY, &amt_m);
            if (ledger_apply_expense(
                    pool, user_id, asset_id, amt_m, is_inc, de_id, note ? note : "AI 批量记账") !=
                0) {
                db_tx_scope_rollback(pool, &scope);
                csilk_json_free(results_arr);
                if (out_code) {
                    *out_code = 500;
                }
                if (err_buf && err_len > 0) {
                    snprintf(err_buf, err_len, "动作 #%zu 联动账户余额失败", i + 1);
                }
                return -1;
            }

            csilk_json_t* item_res = csilk_json_object();
            csilk_json_add_string(item_res, "action_type", "daily_expense");
            csilk_json_add_number(item_res, "id", (double)de_id);
            csilk_json_add_bool(item_res, "success", true);
            csilk_json_array_append(results_arr, item_res);
        } else if (strcmp(action_type, "transfer") == 0) {
            int64_t from_id = (int64_t)db_get_num(it, "from_asset_id");
            int64_t to_id = (int64_t)db_get_num(it, "to_asset_id");
            if (from_id <= 0 || to_id <= 0 || from_id == to_id) {
                db_tx_scope_rollback(pool, &scope);
                csilk_json_free(results_arr);
                if (out_code) {
                    *out_code = 1002;
                }
                if (err_buf && err_len > 0) {
                    snprintf(err_buf, err_len, "动作 #%zu 转账双方账户无效或相同", i + 1);
                }
                return -1;
            }

            int64_t transfer_id = mf_ai_repo_transfer_insert(
                pool, user_id, from_id, to_id, amount, "CNY", dt, note ? note : "");
            if (transfer_id <= 0) {
                db_tx_scope_rollback(pool, &scope);
                csilk_json_free(results_arr);
                if (out_code) {
                    *out_code = 500;
                }
                if (err_buf && err_len > 0) {
                    snprintf(err_buf, err_len, "动作 #%zu 写入转账失败", i + 1);
                }
                return -1;
            }

            money_t amt_m;
            money_from_double(amount, CURRENCY_CNY, &amt_m);
            if (ledger_apply_transfer(pool,
                                      user_id,
                                      from_id,
                                      to_id,
                                      amt_m,
                                      transfer_id,
                                      note ? note : "AI 批量转账") != 0) {
                db_tx_scope_rollback(pool, &scope);
                csilk_json_free(results_arr);
                if (out_code) {
                    *out_code = 500;
                }
                if (err_buf && err_len > 0) {
                    snprintf(err_buf, err_len, "动作 #%zu 联动转账余额失败", i + 1);
                }
                return -1;
            }

            csilk_json_t* item_res = csilk_json_object();
            csilk_json_add_string(item_res, "action_type", "transfer");
            csilk_json_add_number(item_res, "id", (double)transfer_id);
            csilk_json_add_bool(item_res, "success", true);
            csilk_json_array_append(results_arr, item_res);
        } else {
            db_tx_scope_rollback(pool, &scope);
            csilk_json_free(results_arr);
            if (out_code) {
                *out_code = 1002;
            }
            if (err_buf && err_len > 0) {
                snprintf(err_buf, err_len, "动作 #%zu 未知动作类型: %s", i + 1, action_type);
            }
            return -1;
        }
    }

    db_tx_scope_commit(pool, &scope);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "executed_count", (double)cnt);
    csilk_json_add_array(resp, "action_results", results_arr);
    if (out_data) {
        *out_data = resp;
    } else {
        csilk_json_free(resp);
    }
    if (out_code) {
        *out_code = 0;
    }
    return 0;
}
