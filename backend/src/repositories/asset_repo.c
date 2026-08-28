#include "repositories/asset_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

csilk_json_t*
asset_list(csilk_db_pool_t* pool,
           int64_t          user_id,
           int64_t          page,
           int64_t          page_size,
           const char*      category_id,
           int64_t*         total)
{
    char uid[32], limit[32], offset[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(limit, sizeof(limit), "%lld", (long long)page_size);
    snprintf(offset, sizeof(offset), "%lld", (long long)((page - 1) * page_size));
    char        sql[1024], count_sql[512];
    const char *params[8], *cnt_params[4];
    int         pidx = 0, cnt_pidx = 0;
    params[pidx++] = uid;
    cnt_params[cnt_pidx++] = uid;
    if (category_id && category_id[0]) {
        snprintf(
            sql,
            sizeof(sql),
            "SELECT "
            "a.id,a.category_id,a.name,a.account_no,a.symbol,a.quote_source,CAST(a.last_sync_at AS "
            "TEXT) as last_sync_at,"
            "a.current_value,a.currency,a.note,a.created_at,"
            "a.updated_at,c.name as category_name,c.asset_type,a.quantity,a.cost_basis,a.net_value "
            "FROM assets a LEFT JOIN categories c ON a.category_id=c.id WHERE a.user_id=? AND "
            "a.category_id=? ORDER BY c.name,a.name LIMIT ? OFFSET ?");
        snprintf(count_sql,
                 sizeof(count_sql),
                 "SELECT COUNT(*) AS cnt FROM assets a WHERE a.user_id=? AND a.category_id=?");
        params[pidx++] = category_id;
        cnt_params[cnt_pidx++] = category_id;
    } else {
        snprintf(
            sql,
            sizeof(sql),
            "SELECT "
            "a.id,a.category_id,a.name,a.account_no,a.symbol,a.quote_source,CAST(a.last_sync_at AS "
            "TEXT) as last_sync_at,"
            "a.current_value,a.currency,a.note,a.created_at,"
            "a.updated_at,c.name as category_name,c.asset_type,a.quantity,a.cost_basis,a.net_value "
            "FROM assets a LEFT JOIN categories c ON a.category_id=c.id WHERE a.user_id=? ORDER BY "
            "c.name,a.name LIMIT ? OFFSET ?");
        snprintf(
            count_sql, sizeof(count_sql), "SELECT COUNT(*) AS cnt FROM assets a WHERE a.user_id=?");
    }
    params[pidx++] = limit;
    params[pidx++] = offset;
    params[pidx] = NULL;
    cnt_params[cnt_pidx] = NULL;
    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    *total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }
    return csilk_db_query_param_json(pool, sql, params);
}
csilk_json_t*
asset_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    return csilk_db_query_param_json(
        pool,
        "SELECT "
        "a.id,a.category_id,a.name,a.account_no,a.symbol,a.quote_source,CAST(a.last_sync_at AS "
        "TEXT) as last_sync_at,"
        "a.current_value,a.currency,a.note,a.created_at,a."
        "updated_at,c.name as category_name,c.asset_type,a.quantity,a.cost_basis,a.net_value FROM "
        "assets a LEFT JOIN categories c ON a.category_id=c.id WHERE a.id=? AND a.user_id=?",
        (const char*[]){idstr, uid, NULL});
}
int64_t
asset_insert(csilk_db_pool_t* pool,
             int64_t          user_id,
             int64_t          category_id,
             const char*      name,
             const char*      account_no,
             double           current_value,
             const char*      currency,
             const char*      note,
             double           quantity,
             double           cost_basis,
             double           net_value,
             const char*      symbol,
             const char*      quote_source)
{
    char uid[32], cat[32], val[64], qty[64], cb[64], nv[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(val, sizeof(val), "%.6f", current_value);
    snprintf(qty, sizeof(qty), "%.4f", quantity);
    snprintf(cb, sizeof(cb), "%.4f", cost_basis);
    snprintf(nv, sizeof(nv), "%.4f", net_value);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO assets "
        "(user_id,category_id,name,account_no,current_value,currency,note,quantity,cost_basis,net_"
        "value,symbol,quote_source) VALUES (?,?,?, ?,?, ?,?,?,?,?, ?,?) RETURNING id",
        (const char*[]){uid,
                        cat,
                        name,
                        account_no ? account_no : "",
                        val,
                        currency ? currency : "CNY",
                        note ? note : "",
                        qty,
                        cb,
                        nv,
                        symbol ? symbol : "",
                        quote_source ? quote_source : "",
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
asset_update_basic(csilk_db_pool_t* pool,
                   int64_t          user_id,
                   int64_t          id,
                   const char*      name,
                   const char*      account_no,
                   double           current_value,
                   const char*      currency,
                   const char*      note,
                   const char*      symbol,
                   const char*      quote_source)
{
    char uid[32], idstr[32], val[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    snprintf(val, sizeof(val), "%.6f", current_value);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE assets SET "
        "name=?,account_no=?,current_value=?,currency=?,note=?,symbol=?,"
        "quote_source=?,updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=?",
        (const char*[]){name ? name : "",
                        account_no ? account_no : "",
                        val,
                        currency ? currency : "CNY",
                        note ? note : "",
                        symbol ? symbol : "",
                        quote_source ? quote_source : "",
                        idstr,
                        uid,
                        NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}
int
asset_update_market_quote(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          int64_t          asset_id,
                          double           new_net_value)
{
    char uid[32], aid[32], nv_str[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    snprintf(nv_str, sizeof(nv_str), "%.4f", new_net_value);

    char cond[64] = "1=1";
    if (user_id > 0) {
        snprintf(cond, sizeof(cond), "user_id = %lld", (long long)user_id);
    }

    char sql[512];
    snprintf(sql,
             sizeof(sql),
             "UPDATE assets SET "
             "net_value = ?, "
             "current_value = CASE WHEN quantity > 0 THEN ROUND(quantity * ?, 2) ELSE "
             "current_value END, "
             "last_sync_at = CURRENT_TIMESTAMP, "
             "updated_at = CURRENT_TIMESTAMP "
             "WHERE id = ? AND %s",
             cond);
    const char* params[] = {nv_str, nv_str, aid, NULL};
    return csilk_db_exec_param(pool, sql, params);
}
csilk_json_t*
asset_list_for_sync(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    if (user_id > 0) {
        return csilk_db_query_param_json(
            pool,
            "SELECT id, user_id, category_id, name, symbol, quote_source, "
            "COALESCE(CAST(net_value AS REAL), 0.0) as net_value, "
            "COALESCE(CAST(quantity AS REAL), 0.0) as quantity, "
            "currency "
            "FROM assets WHERE user_id=? AND symbol IS NOT NULL AND symbol != ''",
            (const char*[]){uid, NULL});
    } else {
        return csilk_db_query_json(pool,
                                   "SELECT id, user_id, category_id, name, symbol, quote_source, "
                                   "COALESCE(CAST(net_value AS REAL), 0.0) as net_value, "
                                   "COALESCE(CAST(quantity AS REAL), 0.0) as quantity, "
                                   "currency "
                                   "FROM assets WHERE symbol IS NOT NULL AND symbol != ''");
    }
}
int
asset_update_position(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          id,
                      double           net_value,
                      double           quantity,
                      double           cost_basis)
{
    char uid[32], idstr[32], nv[64], qty[64], cb[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    snprintf(nv, sizeof(nv), "%.4f", net_value);
    snprintf(qty, sizeof(qty), "%.4f", quantity);
    snprintf(cb, sizeof(cb), "%.4f", cost_basis);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE assets SET net_value=?,quantity=?,cost_basis=?,updated_at=CURRENT_TIMESTAMP WHERE "
        "id=? AND user_id=?",
        (const char*[]){nv, qty, cb, idstr, uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}
int
asset_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "DELETE FROM assets WHERE id=? AND user_id=?", (const char*[]){idstr, uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}
int
asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT id FROM assets WHERE id=? AND user_id=?", (const char*[]){idstr, uid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}
char*
asset_get_category_type(csilk_db_pool_t* pool, int64_t user_id, int64_t category_id)
{
    char uid[32], cat[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT asset_type FROM categories WHERE id=? AND user_id=?",
                                  (const char*[]){cat, uid, NULL});
    char* result = NULL;
    if (res && csilk_json_array_size(res) > 0) {
        const char* atype = csilk_json_get_string(csilk_json_array_get(res, 0), "asset_type");
        if (atype) {
            result = strdup(atype);
        }
    }
    if (res) {
        csilk_json_free(res);
    }
    return result;
}
csilk_json_t*
asset_transactions(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    char uid[32], aid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT "
        "id,asset_id,transaction_type,amount,quantity,price_per_unit,currency,transaction_date,"
        "note,created_at FROM transactions WHERE asset_id=? AND user_id=? ORDER BY "
        "transaction_date DESC",
        (const char*[]){aid, uid, NULL});
}
