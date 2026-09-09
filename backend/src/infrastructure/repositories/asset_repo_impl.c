#include "infrastructure/repositories/asset_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- SQL statements inlined from repositories/asset_repo.c --- */

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
        "quote_source=?,updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=? RETURNING id",
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
    char val[64], aid[32], uid[32];
    snprintf(val, sizeof(val), "%.4f", new_net_value);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    csilk_json_t* res = NULL;
    if (user_id > 0) {
        res = csilk_db_query_param_json(
            pool,
            "UPDATE assets SET "
            "net_value = ?, "
            "current_value = CASE WHEN quantity > 0 THEN ROUND(quantity * ?, 2) ELSE current_value "
            "END, "
            "last_sync_at = CURRENT_TIMESTAMP, "
            "updated_at = CURRENT_TIMESTAMP "
            "WHERE id = ? AND user_id = ? RETURNING id",
            (const char*[]){val, val, aid, uid, NULL});
    } else {
        res = csilk_db_query_param_json(
            pool,
            "UPDATE assets SET "
            "net_value = ?, "
            "current_value = CASE WHEN quantity > 0 THEN ROUND(quantity * ?, 2) ELSE current_value "
            "END, "
            "last_sync_at = CURRENT_TIMESTAMP, "
            "updated_at = CURRENT_TIMESTAMP "
            "WHERE id = ? RETURNING id",
            (const char*[]){val, val, aid, NULL});
    }
    int updated = res ? (int)csilk_json_array_size(res) : 0;
    if (res) {
        csilk_json_free(res);
    }
    return updated;
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
        "id=? AND user_id=? RETURNING id",
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
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "DELETE FROM assets WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){idstr, uid, NULL});
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

csilk_json_t*
asset_balance_logs_list(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          page,
                        int64_t          page_size,
                        const char*      asset_id_str,
                        int64_t*         total)
{
    char uid_str[32], limit_buf[32], offset_buf[32], aid_buf[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(limit_buf, sizeof(limit_buf), "%lld", (long long)page_size);
    snprintf(offset_buf, sizeof(offset_buf), "%lld", (long long)((page - 1) * page_size));

    char        count_sql[256];
    const char* cnt_params[4];
    snprintf(count_sql,
             sizeof(count_sql),
             "SELECT COUNT(*) AS cnt FROM asset_balance_logs abl WHERE abl.user_id=?");
    cnt_params[0] = uid_str;
    int cnt_pidx = 1;

    csilk_json_t* result = NULL;
    if (asset_id_str && strlen(asset_id_str) > 0) {
        snprintf(aid_buf, sizeof(aid_buf), "%lld", atoll(asset_id_str));
        const char* params[] = {uid_str, aid_buf, limit_buf, offset_buf, NULL};
        result = csilk_db_query_param_json(
            pool,
            "SELECT abl.id, abl.asset_id, a.name AS asset_name, abl.user_id, "
            "abl.delta, abl.balance_after, abl.source_type, abl.source_id, "
            "abl.note, abl.created_at "
            "FROM asset_balance_logs abl "
            "LEFT JOIN assets a ON abl.asset_id = a.id "
            "WHERE abl.user_id=? AND abl.asset_id=? "
            "ORDER BY abl.created_at DESC LIMIT ? OFFSET ?",
            params);
        snprintf(count_sql + strlen(count_sql),
                 sizeof(count_sql) - strlen(count_sql),
                 " AND abl.asset_id=?");
        cnt_params[cnt_pidx++] = aid_buf;
    } else {
        const char* params[] = {uid_str, limit_buf, offset_buf, NULL};
        result = csilk_db_query_param_json(
            pool,
            "SELECT abl.id, abl.asset_id, a.name AS asset_name, abl.user_id, "
            "abl.delta, abl.balance_after, abl.source_type, abl.source_id, "
            "abl.note, abl.created_at "
            "FROM asset_balance_logs abl "
            "LEFT JOIN assets a ON abl.asset_id = a.id "
            "WHERE abl.user_id=? "
            "ORDER BY abl.created_at DESC LIMIT ? OFFSET ?",
            params);
    }
    cnt_params[cnt_pidx] = NULL;

    *total = 0;
    if (!result) {
        return NULL;
    }

    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }

    return result;
}

/* --- Domain repository implementation (mf_asset_repo_*) --- */

int
mf_asset_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_asset_t* out_asset)
{
    if (!out_asset || !db_pool) {
        return -1;
    }
    memset(out_asset, 0, sizeof(*out_asset));
    csilk_json_t* res = asset_get((csilk_db_pool_t*)db_pool, user_id, id);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1;
    }
    csilk_json_t* r = csilk_json_array_get(res, 0);
    out_asset->id = db_get_int(r, "id");
    out_asset->user_id = user_id;
    out_asset->category_id = db_get_int(r, "category_id");
    const char* s = csilk_json_get_string(r, "name");
    if (s) {
        snprintf(out_asset->name, sizeof(out_asset->name), "%s", s);
    }
    s = csilk_json_get_string(r, "account_no");
    if (s) {
        snprintf(out_asset->account_no, sizeof(out_asset->account_no), "%s", s);
    }
    s = csilk_json_get_string(r, "symbol");
    if (s) {
        snprintf(out_asset->symbol, sizeof(out_asset->symbol), "%s", s);
    }
    s = csilk_json_get_string(r, "quote_source");
    if (s) {
        snprintf(out_asset->quote_source, sizeof(out_asset->quote_source), "%s", s);
    }
    s = csilk_json_get_string(r, "currency");
    if (s) {
        out_asset->currency = currency_from_str(s);
    } else {
        out_asset->currency = currency_from_str("CNY");
    }
    currency_t cur = out_asset->currency;
    out_asset->current_value = db_get_money(r, "current_value", cur);
    s = csilk_json_get_string(r, "note");
    if (s) {
        snprintf(out_asset->note, sizeof(out_asset->note), "%s", s);
    }
    {
        double qty_d = db_get_num(r, "quantity");
        quantity_from_double(qty_d, 4, &out_asset->quantity);
    }
    out_asset->cost_basis = db_get_money(r, "cost_basis", cur);
    {
        double nv_d = db_get_num(r, "net_value");
        price_from_double(nv_d, 4, out_asset->currency, &out_asset->net_value);
    }
    csilk_json_free(res);
    return 0;
}

int
mf_asset_repo_save(void* db_pool, const mf_asset_t* asset, int64_t* out_id)
{
    if (!db_pool || !asset) {
        return -1;
    }
    int64_t id = asset_insert((csilk_db_pool_t*)db_pool,
                              asset->user_id,
                              asset->category_id,
                              asset->name,
                              asset->account_no,
                              money_to_double(asset->current_value),
                              currency_code(&asset->currency),
                              asset->note,
                              quantity_to_double(asset->quantity),
                              money_to_double(asset->cost_basis),
                              price_to_double(asset->net_value),
                              asset->symbol,
                              asset->quote_source);
    if (id <= 0) {
        return -1;
    }
    if (out_id) {
        *out_id = id;
    }
    return 0;
}

int
mf_asset_repo_update_basic(void* db_pool, const mf_asset_t* asset)
{
    if (!db_pool || !asset || asset->id <= 0) {
        return -1;
    }
    return asset_update_basic((csilk_db_pool_t*)db_pool,
                              asset->user_id,
                              asset->id,
                              asset->name,
                              asset->account_no,
                              money_to_double(asset->current_value),
                              currency_code(&asset->currency),
                              asset->note,
                              asset->symbol,
                              asset->quote_source);
}

int
mf_asset_repo_update_position(void*      db_pool,
                              int64_t    user_id,
                              int64_t    id,
                              price_t    net_value,
                              quantity_t quantity,
                              money_t    cost_basis)
{
    if (!db_pool || id <= 0) {
        return -1;
    }
    return asset_update_position((csilk_db_pool_t*)db_pool,
                                 user_id,
                                 id,
                                 price_to_double(net_value),
                                 quantity_to_double(quantity),
                                 money_to_double(cost_basis));
}

int
mf_asset_repo_delete(void* db_pool, int64_t user_id, int64_t id)
{
    if (!db_pool || id <= 0) {
        return -1;
    }
    return asset_delete((csilk_db_pool_t*)db_pool, user_id, id);
}

int
mf_asset_repo_exists(void* db_pool, int64_t user_id, int64_t id)
{
    if (!db_pool || id <= 0) {
        return 0;
    }
    return asset_exists((csilk_db_pool_t*)db_pool, user_id, id);
}

int
mf_asset_repo_get_category_type(
    void* db_pool, int64_t user_id, int64_t category_id, char* out_type, size_t out_cap)
{
    if (!db_pool || !out_type || out_cap == 0) {
        return -1;
    }
    out_type[0] = '\0';
    char* atype = asset_get_category_type((csilk_db_pool_t*)db_pool, user_id, category_id);
    if (atype) {
        snprintf(out_type, out_cap, "%s", atype);
        free(atype);
        return 0;
    }
    return -1;
}
