/** @file ai_repo_impl.c @brief AI domain repository implementation with inlined SQL */

#include "infrastructure/repositories/ai_repo_impl.h"
#include "infrastructure/repositories/market_repo_impl.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "infrastructure/repositories/transaction_repo_impl.h"
#include "common/db.h"
#include <string.h>

csilk_json_t*
mf_ai_repo_asset_list(
    void* pool, int64_t user_id, int64_t page, int64_t page_size, const char* type, int64_t* total)
{
    return asset_list(pool, user_id, page, page_size, type, total);
}

csilk_json_t*
mf_ai_repo_asset_get(void* pool, int64_t user_id, int64_t id)
{
    return asset_get(pool, user_id, id);
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
                              int64_t*    total)
{
    char uid[32], limit_str[32], offset_str[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(limit_str, sizeof(limit_str), "%lld", (long long)page_size);
    snprintf(offset_str, sizeof(offset_str), "%lld", (long long)((page - 1) * page_size));

    char        sql[2048], count_sql[1024];
    const char* params[16];
    const char* cnt_params[16];
    int         pidx = 0, cnt_pidx = 0;
    params[pidx++] = uid;
    cnt_params[cnt_pidx++] = uid;

    snprintf(
        sql,
        sizeof(sql),
        "SELECT de.id,de.user_id,de.category_id,de.asset_id,de.expense_type,de.amount,"
        "de.currency,de.expense_date,de.note,de.created_at,de.updated_at,"
        "c.name as category_name,a.name as asset_name,"
        "(SELECT json_group_array(json_object('id',t.id,'name',t.name,'color',t.color)) "
        "FROM expense_tags et JOIN tags t ON et.tag_id=t.id WHERE et.expense_id=de.id) as tags "
        "FROM daily_expenses de LEFT JOIN categories c ON de.category_id=c.id "
        "LEFT JOIN assets a ON de.asset_id=a.id WHERE de.user_id=?");
    snprintf(count_sql,
             sizeof(count_sql),
             "SELECT COUNT(*) AS cnt FROM daily_expenses de WHERE de.user_id=?");

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

    return csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
}

int64_t
mf_ai_repo_daily_expense_insert(void*       pool,
                                int64_t     user_id,
                                const char* expense_date,
                                const char* expense_type,
                                double      amount,
                                const char* currency,
                                int64_t     category_id,
                                int64_t     asset_id,
                                const char* note)
{
    char uid[32], cat[32], ast[32], amt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    csilk_json_t* res =
        csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                  "INSERT INTO daily_expenses "
                                  "(user_id,category_id,asset_id,expense_type,amount,currency,"
                                  "expense_date,note) VALUES (?,?,?,?,?,?,?,?) RETURNING id",
                                  (const char*[]){uid,
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
    return tx_list(pool, user_id, page, page_size, NULL, NULL, type, NULL, NULL, NULL, total);
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
        "WHERE de.user_id=? AND de.expense_date LIKE ? "
        "GROUP BY c.name,de.expense_type ORDER BY amount DESC",
        (const char*[]){uid, uid, pattern, NULL});
}

csilk_json_t*
mf_ai_repo_daily_expense_monthly_totals(void* pool, int64_t user_id, const char* pattern)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as "
        "total_income,COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as "
        "total_expense FROM daily_expenses WHERE user_id=? AND expense_date LIKE ?",
        (const char*[]){uid, pattern, NULL});
}

csilk_json_t*
mf_ai_repo_transaction_monthly(void* pool, int64_t user_id, const char* pattern)
{
    return tx_monthly(pool, user_id, pattern);
}

int64_t
mf_ai_repo_transfer_insert(void*       pool,
                           int64_t     user_id,
                           int64_t     from_asset_id,
                           int64_t     to_asset_id,
                           double      amount,
                           const char* currency,
                           const char* note)
{
    char uid[32], fid[32], tid[32], amt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)from_asset_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)to_asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "INSERT INTO transfers (user_id, from_asset_id, to_asset_id, amount, currency, "
        "transfer_date, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id",
        (const char*[]){
            uid, fid, tid, amt, currency ? currency : "CNY", "", note ? note : "", NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}
