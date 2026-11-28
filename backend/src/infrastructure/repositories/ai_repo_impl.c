/** @file ai_repo_impl.c @brief AI domain repository implementation with inlined SQL
 *  注意：tags/嵌套列在 DB层永远是字符串标量，必须 csilk_json_parse一次（见 db.c:122）
 */

#include "infrastructure/repositories/ai_repo_impl.h"
#include "infrastructure/repositories/market_repo_impl.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "infrastructure/repositories/transaction_repo_impl.h"
#include "infrastructure/repositories/daily_expense_repo_impl.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include <string.h>
#include <time.h>

csilk_json_t*
mf_ai_repo_asset_list(
    void* pool, int64_t user_id, int64_t page, int64_t page_size, const char* type, int64_t* total)
{
    return asset_list(pool, user_id, 0, 0, page, page_size, type, 0, total);
}

csilk_json_t*
mf_ai_repo_asset_get(void* pool, int64_t user_id, int64_t id)
{
    return asset_get(pool, user_id, 0, id);
}

csilk_json_t*
mf_ai_repo_price_history_list(void* pool, int64_t user_id, int64_t asset_id, int64_t limit)
{
    return mf_market_repo_price_history_list(pool, user_id, asset_id, limit);
}

csilk_json_t*
mf_ai_repo_daily_expense_list(void*       pool,
                              int64_t     user_id,
                              int64_t     page,
                              int64_t     page_size,
                              const char* date_from,
                              const char* date_to,
                              int64_t     category_id,
                              const char* expense_type,
                              int64_t*    total)
{
    char uid[32], limit_str[32], offset_str[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(limit_str, sizeof(limit_str), "%lld", (long long)page_size);
    snprintf(offset_str, sizeof(offset_str), "%lld", (long long)((page - 1) * page_size));

    char        sql[2560], count_sql[1536];
    const char* params[24];
    const char* cnt_params[20];
    int         pidx = 0, cnt_pidx = 0;
    params[pidx++] = uid;
    params[pidx++] = uid;
    params[pidx++] = uid;
    params[pidx++] = uid;
    params[pidx++] = uid;

    cnt_params[cnt_pidx++] = uid;
    cnt_params[cnt_pidx++] = uid;
    cnt_params[cnt_pidx++] = uid;

    snprintf(
        sql,
        sizeof(sql),
        "SELECT de.id,de.user_id,de.category_id,de.asset_id,de.expense_type,de.amount,"
        "de.currency,de.expense_date,de.note,de.created_at,de.updated_at,"
        "c.name as category_name,a.name as asset_name,"
        "(SELECT %s "
        "FROM expense_tags et JOIN tags t ON et.tag_id=t.id WHERE et.expense_id=de.id) as "
        "tags," MF_RBAC_MASK_BALANCE_COL(
            "a") " AS mask_balance "
                 "FROM daily_expenses de LEFT JOIN categories c ON de.category_id=c.id "
                 "LEFT JOIN assets a ON de.asset_id=a.id "
                 "WHERE de.user_id=? AND " MF_RBAC_ASSET_VISIBLE_SQL("a"),
        db_is_postgres()
            ? "COALESCE(json_agg(json_build_object('id',t.id,'name',t.name,'color',t.color)),'[]')"
            : "json_group_array(json_object('id',t.id,'name',t.name,'color',t.color))");
    snprintf(count_sql,
             sizeof(count_sql),
             "SELECT COUNT(*) AS cnt FROM daily_expenses de "
             "LEFT JOIN assets a ON de.asset_id=a.id "
             "WHERE de.user_id=? AND " MF_RBAC_ASSET_VISIBLE_SQL("a"));

    if (category_id > 0) {
        char cat_str[32];
        snprintf(cat_str, sizeof(cat_str), "%lld", (long long)category_id);
        strncat(sql, " AND de.category_id=?", sizeof(sql) - strlen(sql) - 1);
        strncat(count_sql, " AND de.category_id=?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = cat_str;
        cnt_params[cnt_pidx++] = cat_str;
    }
    if (date_from && date_from[0]) {
        strncat(sql, " AND de.expense_date >= ?", sizeof(sql) - strlen(sql) - 1);
        strncat(count_sql, " AND de.expense_date >= ?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = date_from;
        cnt_params[cnt_pidx++] = date_from;
    }
    if (expense_type &&
        (strcmp(expense_type, "expense") == 0 || strcmp(expense_type, "income") == 0)) {
        strncat(sql, " AND de.expense_type=?", sizeof(sql) - strlen(sql) - 1);
        strncat(count_sql, " AND de.expense_type=?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = expense_type;
        cnt_params[cnt_pidx++] = expense_type;
    }
    if (date_to && date_to[0]) {
        strncat(sql, " AND de.expense_date <= ?", sizeof(sql) - strlen(sql) - 1);
        strncat(count_sql, " AND de.expense_date <= ?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = date_to;
        cnt_params[cnt_pidx++] = date_to;
    }
    strncat(sql, " ORDER BY de.expense_date DESC LIMIT ? OFFSET ?", sizeof(sql) - strlen(sql) - 1);
    params[pidx++] = limit_str;
    params[pidx++] = offset_str;
    params[pidx] = NULL;
    cnt_params[cnt_pidx] = NULL;

    csilk_json_t* cnt_res =
        csilk_db_query_param_json((csilk_db_pool_t*)pool, count_sql, cnt_params);
    *total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }

    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
    return res ? mf_format_expense_rows_with_tags(res) : NULL;
}

int64_t
mf_ai_repo_daily_expense_insert(void*       pool,
                                int64_t     user_id,
                                int64_t     ledger_id,
                                const char* expense_date,
                                const char* expense_type,
                                double      amount,
                                const char* currency,
                                int64_t     category_id,
                                int64_t     asset_id,
                                const char* note)
{
    char uid[32], cat[32], ast[32], amt[64], date_buf[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);

    if (!expense_date || !expense_date[0]) {
        time_t    now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);
        expense_date = date_buf;
    }
    if (ledger_id <= 0 && asset_id > 0) {
        csilk_json_t* a_rows =
            csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                      "SELECT ledger_id FROM assets WHERE id = ?",
                                      (const char*[]){ast, NULL});
        if (a_rows && csilk_json_array_size(a_rows) > 0) {
            ledger_id = db_get_int(csilk_json_array_get(a_rows, 0), "ledger_id");
        }
        if (a_rows) {
            csilk_json_free(a_rows);
        }
    }
    if (ledger_id <= 0) {
        ledger_id = ledger_get_default((csilk_db_pool_t*)pool, user_id);
    }
    if (ledger_id <= 0) {
        ledger_id = 1;
    }

    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "INSERT INTO daily_expenses "
        "(user_id,ledger_id,category_id,asset_id,expense_type,amount,currency,"
        "expense_date,note) VALUES (?,?,?,?,?,?,?,?,?) RETURNING id",
        (const char*[]){uid,
                        lid,
                        cat,
                        ast,
                        expense_type,
                        amt,
                        currency ? currency : "CNY",
                        expense_date,
                        note ? note : "",
                        NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

csilk_json_t*
mf_ai_repo_category_list(void* pool, int64_t user_id, const char* type)
{
    csilk_db_pool_t* p = (csilk_db_pool_t*)pool;
    char             uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    if (type && type[0]) {
        return csilk_db_query_param_json(
            p,
            "SELECT c.id,c.name,c.parent_id,c.type,c.asset_type,c.currency,c.icon,c.sort_"
            "order,c.created_at FROM categories c WHERE c.user_id=? AND c.type=? ORDER BY "
            "c.sort_order,c.name",
            (const char*[]){uid, type, NULL});
    }
    return csilk_db_query_param_json(
        p,
        "SELECT c.id,c.name,c.parent_id,c.type,c.asset_type,c.currency,c.icon,c.sort_"
        "order,c.created_at FROM categories c WHERE c.user_id=? ORDER BY c.sort_order,c.name",
        (const char*[]){uid, NULL});
}

csilk_json_t*
mf_ai_repo_transaction_list(
    void* pool, int64_t user_id, int64_t page, int64_t page_size, const char* type, int64_t* total)
{
    return tx_list(pool, user_id, 0, page, page_size, NULL, NULL, type, NULL, NULL, NULL, total);
}

csilk_json_t*
mf_ai_repo_daily_expense_monthly_by_category(void* pool, int64_t user_id, const char* pattern)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT c.name as category_name,de.expense_type,SUM(de.amount) as amount FROM "
        "daily_expenses de JOIN categories c ON de.category_id=c.id AND c.user_id=? "
        "LEFT JOIN assets a ON de.asset_id=a.id "
        "WHERE de.user_id=? AND CAST(de.expense_date AS TEXT) LIKE ? "
        "AND " MF_RBAC_ASSET_UNMASKED_SQL(
            "a") " "
                 "GROUP BY c.name,de.expense_type ORDER BY amount DESC",
        (const char*[]){uid, uid, pattern, uid, uid, NULL});
}

csilk_json_t*
mf_ai_repo_daily_expense_hist_avg_by_category(void*       pool,
                                              int64_t     user_id,
                                              const char* exclude_month_pattern)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    const char* excl = (exclude_month_pattern && exclude_month_pattern[0]) ? exclude_month_pattern
                                                                           : "0000-00%";
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT category_id, COALESCE(category_name,'未分类') as category_name, "
        "AVG(m_sum) as avg_amount FROM ("
        "  SELECT de.category_id, c.name as category_name, "
        "  substr(CAST(de.expense_date AS TEXT),1,7) as ym, SUM(de.amount) as m_sum "
        "  FROM daily_expenses de "
        "  LEFT JOIN categories c ON c.id = de.category_id "
        "  LEFT JOIN assets _ra ON de.asset_id = _ra.id "
        "  WHERE de.user_id=? AND de.expense_type='expense' "
        "  AND CAST(de.expense_date AS TEXT) NOT LIKE ? "
        "  AND " MF_RBAC_ASSET_UNMASKED_SQL("_ra") " "
                                                   "  GROUP BY de.category_id, c.name, ym"
                                                   ") sub GROUP BY category_id, category_name",
        (const char*[]){uid, excl, uid, uid, NULL});
}

csilk_json_t*
mf_ai_repo_savings_goals_list(void* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT id, name, target_amount, current_amount, "
        "COALESCE(deadline, '') as deadline, COALESCE(note, '') as note, created_at "
        "FROM savings_goals WHERE user_id=? "
        "ORDER BY CASE WHEN deadline IS NULL OR deadline = '' THEN 1 ELSE 0 END, deadline ASC, id "
        "ASC",
        (const char*[]){uid, NULL});
}

int
mf_ai_repo_savings_goal_upsert(void*       pool,
                               int64_t     user_id,
                               int64_t     goal_id,
                               const char* name,
                               double      target_amount,
                               double      current_amount,
                               const char* deadline,
                               const char* note,
                               int64_t*    out_id)
{
    if (!pool || user_id <= 0 || !name || !name[0] || target_amount <= 0.0) {
        return -1;
    }
    char uid[32], tgt_s[64], cur_s[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(tgt_s, sizeof(tgt_s), "%.2f", target_amount);
    snprintf(cur_s, sizeof(cur_s), "%.2f", current_amount >= 0.0 ? current_amount : 0.0);
    const char* dl = (deadline && deadline[0]) ? deadline : "";
    const char* nt = (note && note[0]) ? note : "";

    csilk_json_t* res = NULL;
    if (goal_id > 0) {
        char gid[32];
        snprintf(gid, sizeof(gid), "%lld", (long long)goal_id);
        res = csilk_db_query_param_json(
            (csilk_db_pool_t*)pool,
            "UPDATE savings_goals SET name=?, target_amount=?, current_amount=?, deadline=?, "
            "note=? WHERE id=? AND user_id=? RETURNING id",
            (const char*[]){name, tgt_s, cur_s, dl, nt, gid, uid, NULL});
    } else {
        res = csilk_db_query_param_json(
            (csilk_db_pool_t*)pool,
            "INSERT INTO savings_goals (user_id, name, target_amount, current_amount, deadline, "
            "note) VALUES (?, ?, ?, ?, ?, ?) RETURNING id",
            (const char*[]){uid, name, tgt_s, cur_s, dl, nt, NULL});
    }
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }
    if (out_id) {
        *out_id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    csilk_json_free(res);
    return 0;
}

csilk_json_t*
mf_ai_repo_daily_expense_monthly_totals(void* pool, int64_t user_id, const char* pattern)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT COALESCE(SUM(CASE WHEN de.expense_type='income' THEN de.amount ELSE 0 END),0) as "
        "total_income,COALESCE(SUM(CASE WHEN de.expense_type='expense' THEN de.amount ELSE 0 "
        "END),0) as total_expense FROM daily_expenses de "
        "LEFT JOIN assets a ON de.asset_id=a.id "
        "WHERE de.user_id=? AND CAST(de.expense_date AS TEXT) LIKE ? "
        "AND " MF_RBAC_ASSET_UNMASKED_SQL("a"),
        (const char*[]){uid, pattern, uid, uid, NULL});
}

csilk_json_t*
mf_ai_repo_transaction_monthly(void* pool, int64_t user_id, const char* pattern)
{
    return tx_monthly(pool, user_id, 0, pattern);
}

int64_t
mf_ai_repo_transfer_insert(void*       pool,
                           int64_t     user_id,
                           int64_t     from_asset_id,
                           int64_t     to_asset_id,
                           double      amount,
                           const char* currency,
                           const char* transfer_date,
                           const char* note)
{
    char uid[32], fid[32], tid[32], amt[64], date_buf[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)from_asset_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)to_asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);

    if (!transfer_date || !transfer_date[0]) {
        time_t    now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", &tm_now);
        transfer_date = date_buf;
    }

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "INSERT INTO transfers (user_id, from_asset_id, to_asset_id, amount, currency, "
        "transfer_date, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id",
        (const char*[]){uid,
                        fid,
                        tid,
                        amt,
                        currency ? currency : "CNY",
                        transfer_date,
                        note ? note : "",
                        NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

static char*
ai_repo_str_replace_first(const char* src, const char* target, const char* replacement)
{
    if (!src || !target || !replacement) {
        return NULL;
    }
    const char* pos = strstr(src, target);
    if (!pos) {
        return NULL;
    }
    size_t target_len = strlen(target);
    size_t repl_len = strlen(replacement);
    size_t prefix_len = (size_t)(pos - src);
    size_t suffix_len = strlen(pos + target_len);
    size_t new_len = prefix_len + repl_len + suffix_len;

    char* out = (char*)malloc(new_len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, src, prefix_len);
    memcpy(out + prefix_len, replacement, repl_len);
    memcpy(out + prefix_len + repl_len, pos + target_len, suffix_len);
    out[new_len] = '\0';
    return out;
}

static char*
ai_repo_mark_content_executed(const char* content, const char* draft_token)
{
    if (!content || !content[0] || !draft_token || !draft_token[0]) {
        return NULL;
    }
    if (!strstr(content, draft_token)) {
        return NULL;
    }
    if (strstr(content, "\"status\":\"executed\"") || strstr(content, "\"status\": \"executed\"")) {
        return NULL;
    }
    const char* target = NULL;
    const char* replacement = NULL;
    if (strstr(content, "\"status\": \"pending\"")) {
        target = "\"status\": \"pending\"";
        replacement = "\"status\": \"executed\"";
    } else if (strstr(content, "\"status\":\"pending\"")) {
        target = "\"status\":\"pending\"";
        replacement = "\"status\":\"executed\"";
    } else if (strstr(content, "\"status\": \"proposed\"")) {
        target = "\"status\": \"proposed\"";
        replacement = "\"status\": \"executed\"";
    } else if (strstr(content, "\"status\":\"proposed\"")) {
        target = "\"status\":\"proposed\"";
        replacement = "\"status\":\"executed\"";
    } else if (strstr(content, "\"action_type\"")) {
        target = "\"action_type\"";
        replacement = "\"status\":\"executed\",\"action_type\"";
    }
    if (target && replacement) {
        return ai_repo_str_replace_first(content, target, replacement);
    }
    return NULL;
}

static char*
ai_repo_mark_tool_calls_executed(const char* tool_calls, const char* draft_token)
{
    if (!tool_calls || !tool_calls[0] || !draft_token || !draft_token[0]) {
        return NULL;
    }
    if (!strstr(tool_calls, draft_token)) {
        return NULL;
    }
    if (strstr(tool_calls, "\\\"status\\\":\\\"executed\\\"") ||
        strstr(tool_calls, "\\\"status\\\": \\\"executed\\\"") ||
        strstr(tool_calls, "\"status\":\"executed\"") ||
        strstr(tool_calls, "\"status\": \"executed\"")) {
        return NULL;
    }
    const char* target = NULL;
    const char* replacement = NULL;
    if (strstr(tool_calls, "\\\"status\\\": \\\"pending\\\"")) {
        target = "\\\"status\\\": \\\"pending\\\"";
        replacement = "\\\"status\\\": \\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"status\\\":\\\"pending\\\"")) {
        target = "\\\"status\\\":\\\"pending\\\"";
        replacement = "\\\"status\\\":\\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"status\\\": \\\"proposed\\\"")) {
        target = "\\\"status\\\": \\\"proposed\\\"";
        replacement = "\\\"status\\\": \\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"status\\\":\\\"proposed\\\"")) {
        target = "\\\"status\\\":\\\"proposed\\\"";
        replacement = "\\\"status\\\":\\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"propose_success\\\":true")) {
        target = "\\\"propose_success\\\":true";
        replacement = "\\\"propose_success\\\":true,\\\"status\\\":\\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"propose_success\\\": true")) {
        target = "\\\"propose_success\\\": true";
        replacement = "\\\"propose_success\\\": true,\\\"status\\\": \\\"executed\\\"";
    } else if (strstr(tool_calls, "\\\"action_type\\\"")) {
        target = "\\\"action_type\\\"";
        replacement = "\\\"status\\\":\\\"executed\\\",\\\"action_type\\\"";
    } else if (strstr(tool_calls, "\"status\": \"pending\"")) {
        target = "\"status\": \"pending\"";
        replacement = "\"status\": \"executed\"";
    } else if (strstr(tool_calls, "\"status\":\"pending\"")) {
        target = "\"status\":\"pending\"";
        replacement = "\"status\":\"executed\"";
    } else if (strstr(tool_calls, "\"action_type\"")) {
        target = "\"action_type\"";
        replacement = "\"status\":\"executed\",\"action_type\"";
    }
    if (target && replacement) {
        return ai_repo_str_replace_first(tool_calls, target, replacement);
    }
    return NULL;
}

int
mf_ai_repo_mark_draft_executed(void* pool, int64_t user_id, const char* draft_token)
{
    if (!pool || user_id <= 0 || !draft_token || !draft_token[0]) {
        return -1;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%%%s%%", draft_token);

    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT id, content, tool_calls FROM ai_messages "
        "WHERE session_id IN (SELECT id FROM ai_sessions WHERE user_id = ?) "
        "  AND (content LIKE ? OR tool_calls LIKE ?) "
        "ORDER BY id DESC LIMIT 1",
        (const char*[]){uid_str, pattern, pattern, NULL});

    if (!rows || !csilk_json_is_array(rows) || csilk_json_array_size(rows) == 0) {
        if (rows) {
            csilk_json_free(rows);
        }
        return 0;
    }

    const csilk_json_t* row = csilk_json_array_get(rows, 0);
    int64_t             msg_id = db_get_int(row, "id");
    const char*         content = csilk_json_get_string(row, "content");
    const char*         tool_calls = csilk_json_get_string(row, "tool_calls");

    if (msg_id > 0) {
        char* new_content = ai_repo_mark_content_executed(content, draft_token);
        char* new_tool_calls = ai_repo_mark_tool_calls_executed(tool_calls, draft_token);

        if (new_content || new_tool_calls) {
            char mid_str[32];
            snprintf(mid_str, sizeof(mid_str), "%lld", (long long)msg_id);
            const char* final_content = new_content ? new_content : (content ? content : "");
            const char* final_tc = new_tool_calls ? new_tool_calls : (tool_calls ? tool_calls : "");

            csilk_db_exec_param((csilk_db_pool_t*)pool,
                                "UPDATE ai_messages SET content = ?, tool_calls = ? WHERE id = ?",
                                (const char*[]){final_content, final_tc, mid_str, NULL});
        }
        if (new_content) {
            free(new_content);
        }
        if (new_tool_calls) {
            free(new_tool_calls);
        }
    }
    csilk_json_free(rows);
    return 0;
}
