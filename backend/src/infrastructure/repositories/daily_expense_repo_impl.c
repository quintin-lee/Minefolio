/** @file daily_expense_repo_impl.c @brief Daily expense repository with inlined SQL */

#include "infrastructure/repositories/daily_expense_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

int
mf_daily_expense_repo_list(void*          db_pool,
                           int64_t        user_id,
                           int64_t        ledger_id,
                           int64_t        page,
                           int64_t        page_size,
                           const char*    expense_type,
                           const char*    category_id,
                           const char*    tag_ids,
                           const char*    start_date,
                           const char*    end_date,
                           csilk_json_t** out_json,
                           int64_t*       out_total)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid[32], lid[32], limit_s[32], offset_s[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(limit_s, sizeof(limit_s), "%lld", (long long)page_size);
    snprintf(offset_s, sizeof(offset_s), "%lld", (long long)((page - 1) * page_size));

    char        sql[2048], count_sql[1024];
    const char* params[16];
    const char* cnt_params[16];
    int         pidx = 0, cnt_pidx = 0;
    params[pidx++] = uid;
    params[pidx++] = lid;
    cnt_params[cnt_pidx++] = uid;
    cnt_params[cnt_pidx++] = lid;

    snprintf(
        sql,
        sizeof(sql),
        "SELECT de.id,de.user_id,de.category_id,de.asset_id,de.expense_type,de.amount,"
        "de.currency,de.expense_date,de.note,de.created_at,de.updated_at,"
        "c.name as category_name,a.name as asset_name,"
        "(SELECT json_group_array(json_object('id',t.id,'name',t.name,'color',t.color)) "
        "FROM expense_tags et JOIN tags t ON et.tag_id=t.id WHERE et.expense_id=de.id) as tags "
        "FROM daily_expenses de LEFT JOIN categories c ON de.category_id=c.id "
        "AND c.user_id=de.user_id AND c.ledger_id=de.ledger_id "
        "LEFT JOIN assets a ON de.asset_id=a.id AND a.user_id=de.user_id AND "
        "a.ledger_id=de.ledger_id "
        "WHERE de.user_id=? AND de.ledger_id=?");
    snprintf(count_sql,
             sizeof(count_sql),
             "SELECT COUNT(*) AS cnt FROM daily_expenses de WHERE de.user_id=? AND de.ledger_id=?");

    if (expense_type && expense_type[0]) {
        strncat(sql, " AND de.expense_type=?", sizeof(sql) - strlen(sql) - 1);
        strncat(count_sql, " AND de.expense_type=?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = expense_type;
        cnt_params[cnt_pidx++] = expense_type;
    }
    if (category_id && category_id[0]) {
        strncat(sql, " AND de.category_id=?", sizeof(sql) - strlen(sql) - 1);
        strncat(count_sql, " AND de.category_id=?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = category_id;
        cnt_params[cnt_pidx++] = category_id;
    }
    if (tag_ids && tag_ids[0]) {
        strncat(sql,
                " AND EXISTS (SELECT 1 FROM expense_tags et2 WHERE et2.expense_id=de.id AND "
                "et2.tag_id IN (SELECT tag_id FROM expense_tags WHERE expense_id=de.id))",
                sizeof(sql) - strlen(sql) - 1);
    }
    if (start_date && start_date[0]) {
        strncat(sql, " AND de.expense_date >= ?", sizeof(sql) - strlen(sql) - 1);
        strncat(count_sql, " AND de.expense_date >= ?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = start_date;
        cnt_params[cnt_pidx++] = start_date;
    }
    if (end_date && end_date[0]) {
        strncat(sql, " AND de.expense_date <= ?", sizeof(sql) - strlen(sql) - 1);
        strncat(count_sql, " AND de.expense_date <= ?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = end_date;
        cnt_params[cnt_pidx++] = end_date;
    }
    strncat(sql, " ORDER BY de.expense_date DESC LIMIT ? OFFSET ?", sizeof(sql) - strlen(sql) - 1);
    params[pidx++] = limit_s;
    params[pidx++] = offset_s;
    params[pidx] = NULL;
    cnt_params[cnt_pidx] = NULL;

    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    *out_total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        *out_total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }

    *out_json = csilk_db_query_param_json(pool, sql, params);
    return *out_json ? 0 : -1;
}

csilk_json_t*
mf_daily_expense_repo_monthly_totals(void*       db_pool,
                                     int64_t     user_id,
                                     int64_t     ledger_id,
                                     const char* pattern)
{
    char uid[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as "
        "total_income,COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as "
        "total_expense FROM daily_expenses WHERE user_id=? AND ledger_id=? AND expense_date LIKE ?",
        (const char*[]){uid, lid, pattern, NULL});
}

csilk_json_t*
mf_daily_expense_repo_monthly_by_category(void*       db_pool,
                                          int64_t     user_id,
                                          int64_t     ledger_id,
                                          const char* pattern)
{
    char uid[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT c.name as category_name,de.expense_type,SUM(de.amount) as amount FROM "
        "daily_expenses de JOIN categories c ON de.category_id=c.id "
        "AND c.user_id=? AND c.ledger_id=? WHERE de.user_id=? AND de.ledger_id=? "
        "AND de.expense_date LIKE ? GROUP BY c.name,de.expense_type ORDER BY amount DESC",
        (const char*[]){uid, lid, uid, lid, pattern, NULL});
}

csilk_json_t*
mf_daily_expense_repo_monthly_by_tag(void*       db_pool,
                                     int64_t     user_id,
                                     int64_t     ledger_id,
                                     const char* pattern)
{
    char uid[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT t.name as tag_name,SUM(de.amount) as amount,COUNT(*) as count FROM daily_expenses "
        "de JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id AND t.user_id=? "
        "WHERE de.user_id=? AND de.ledger_id=? AND de.expense_date LIKE ? "
        "GROUP BY t.name ORDER BY amount DESC",
        (const char*[]){uid, uid, lid, pattern, NULL});
}

csilk_json_t*
mf_daily_expense_repo_monthly_daily(void*       db_pool,
                                    int64_t     user_id,
                                    int64_t     ledger_id,
                                    const char* pattern)
{
    char uid[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT expense_date,COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 "
        "END),0) as income,COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 "
        "END),0) as expense FROM daily_expenses "
        "WHERE user_id=? AND ledger_id=? AND expense_date LIKE ? GROUP BY expense_date "
        "ORDER BY expense_date",
        (const char*[]){uid, lid, pattern, NULL});
}

int64_t
mf_daily_expense_repo_insert(void*       db_pool,
                             int64_t     user_id,
                             int64_t     ledger_id,
                             int64_t     category_id,
                             int64_t     asset_id,
                             const char* expense_type,
                             double      amount,
                             const char* currency,
                             const char* date,
                             const char* note)
{
    char uid[32], lid[32], cat[32], ast[32], amt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
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
                        date,
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

int
mf_daily_expense_repo_get_snapshot(
    void* db_pool, int64_t user_id, int64_t ledger_id, int64_t id, mf_daily_expense_snapshot_t* out)
{
    char uid[32], lid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* row =
        csilk_db_query_param_json((csilk_db_pool_t*)db_pool,
                                  "SELECT amount,expense_type,asset_id,currency,note FROM "
                                  "daily_expenses WHERE id=? AND user_id=? AND ledger_id=?",
                                  (const char*[]){idstr, uid, lid, NULL});
    if (!row || csilk_json_array_size(row) == 0) {
        if (row) {
            csilk_json_free(row);
        }
        return -1;
    }
    const csilk_json_t* r = csilk_json_array_get(row, 0);
    out->amount = db_get_num(r, "amount");
    const char* et = csilk_json_get_string(r, "expense_type");
    strncpy(out->expense_type, et ? et : "", sizeof(out->expense_type) - 1);
    const char* cur = csilk_json_get_string(r, "currency");
    strncpy(out->currency, cur && cur[0] ? cur : "CNY", sizeof(out->currency) - 1);
    out->asset_id = (int64_t)db_get_int(r, "asset_id");
    const char* nt = csilk_json_get_string(r, "note");
    strncpy(out->note, nt ? nt : "", sizeof(out->note) - 1);
    csilk_json_free(row);
    return 0;
}

int
mf_daily_expense_repo_update(void*       db_pool,
                             int64_t     user_id,
                             int64_t     ledger_id,
                             int64_t     id,
                             int64_t     category_id,
                             int64_t     asset_id,
                             const char* expense_type,
                             double      amount,
                             const char* currency,
                             const char* date,
                             const char* note)
{
    char uid[32], lid[32], idstr[32], cat[32], ast[32], amt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "UPDATE daily_expenses SET category_id=?,asset_id=?,expense_type=?,amount=?,currency=?,"
        "expense_date=?,note=?,updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=? AND "
        "ledger_id=? RETURNING id",
        (const char*[]){cat,
                        ast,
                        expense_type ? expense_type : "",
                        amt,
                        currency ? currency : "CNY",
                        date ? date : "",
                        note ? note : "",
                        idstr,
                        uid,
                        lid,
                        NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
mf_daily_expense_repo_delete(void* db_pool, int64_t user_id, int64_t ledger_id, int64_t id)
{
    char uid[32], lid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "DELETE FROM daily_expenses WHERE id=? AND user_id=? AND ledger_id=? RETURNING id",
        (const char*[]){idstr, uid, lid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
mf_daily_expense_repo_exists(void* db_pool, int64_t user_id, int64_t ledger_id, int64_t id)
{
    char uid_str[32], lid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT id FROM daily_expenses WHERE id=? AND user_id=? AND ledger_id=?",
        (const char*[]){id_str, uid_str, lid_str, NULL});
    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : 1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_daily_expense_repo_tag_insert(void* db_pool, int64_t expense_id, int64_t tag_id)
{
    char eid[32], tid[32];
    snprintf(eid, sizeof(eid), "%lld", (long long)expense_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)tag_id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "INSERT OR IGNORE INTO expense_tags (expense_id,tag_id) VALUES (?,?)",
        (const char*[]){eid, tid, NULL});
    int ok = res ? 0 : -1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_daily_expense_repo_tag_delete_all(void* db_pool, int64_t expense_id)
{
    char eid[32];
    snprintf(eid, sizeof(eid), "%lld", (long long)expense_id);
    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)db_pool,
                                                  "DELETE FROM expense_tags WHERE expense_id=?",
                                                  (const char*[]){eid, NULL});
    int           ok = res ? 0 : -1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int64_t
mf_daily_expense_repo_get_or_create_tag(
    void* db_pool, int64_t user_id, int64_t tag_id, const char* tag_name, const char* tag_color)
{
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    if (tag_id > 0) {
        char tid_str[32];
        snprintf(tid_str, sizeof(tid_str), "%lld", (long long)tag_id);
        csilk_json_t* chk =
            csilk_db_query_param_json((csilk_db_pool_t*)db_pool,
                                      "SELECT id FROM tags WHERE id=? AND user_id=?",
                                      (const char*[]){tid_str, uid_str, NULL});
        if (chk && csilk_json_array_size(chk) > 0) {
            int64_t id = db_get_int(csilk_json_array_get(chk, 0), "id");
            csilk_json_free(chk);
            return id;
        }
        if (chk) {
            csilk_json_free(chk);
        }
    }

    if (tag_name && tag_name[0]) {
        csilk_json_t* q_res =
            csilk_db_query_param_json((csilk_db_pool_t*)db_pool,
                                      "SELECT id FROM tags WHERE user_id=? AND name=?",
                                      (const char*[]){uid_str, tag_name, NULL});
        if (q_res && csilk_json_array_size(q_res) > 0) {
            int64_t existing_id = db_get_int(csilk_json_array_get(q_res, 0), "id");
            csilk_json_free(q_res);
            return existing_id;
        }
        if (q_res) {
            csilk_json_free(q_res);
        }

        const char*   color = tag_color && tag_color[0] ? tag_color : "#3b82f6";
        csilk_json_t* ins_res = csilk_db_query_param_json(
            (csilk_db_pool_t*)db_pool,
            "INSERT INTO tags (user_id, name, color) VALUES (?, ?, ?) RETURNING id",
            (const char*[]){uid_str, tag_name, color, NULL});
        if (ins_res && csilk_json_array_size(ins_res) > 0) {
            int64_t new_id = db_get_int(csilk_json_array_get(ins_res, 0), "id");
            csilk_json_free(ins_res);
            return new_id;
        }
        if (ins_res) {
            csilk_json_free(ins_res);
        }
    }
    return 0;
}
