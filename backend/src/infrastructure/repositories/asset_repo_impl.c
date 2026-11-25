#include "infrastructure/repositories/asset_repo_impl.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "domain/account/rules.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 将原始资产查询行格式化为脱敏安全的 JSON 数组，释放 raw_rows */
static csilk_json_t*
format_asset_rows(csilk_json_t* raw_rows, bool include_ledger_id)
{
    if (!raw_rows || !csilk_json_is_array(raw_rows)) {
        return raw_rows;
    }
    size_t        n = csilk_json_array_size(raw_rows);
    csilk_json_t* out = csilk_json_array();
    if (!out) {
        return raw_rows;
    }
    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* r = csilk_json_array_get(raw_rows, i);
        if (!r || !csilk_json_is_object(r)) {
            continue;
        }
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_int(item, "id", db_get_int(r, "id"));
        if (include_ledger_id) {
            csilk_json_add_int(item, "ledger_id", db_get_int(r, "ledger_id"));
        }
        csilk_json_add_int(item, "category_id", db_get_int(r, "category_id"));
        csilk_json_add_string(item, "name", csilk_json_get_string(r, "name"));

        int         mask_acc = db_get_int(r, "mask_account_no");
        const char* raw_acc = csilk_json_get_string(r, "account_no");
        if (mask_acc != 0) {
            char masked_buf[128] = {0};
            mf_account_rule_mask_account_no(raw_acc, masked_buf, sizeof(masked_buf));
            csilk_json_add_string(item, "account_no", masked_buf);
            csilk_json_add_bool(item, "is_account_no_masked", true);
        } else {
            csilk_json_add_string(item, "account_no", raw_acc);
            csilk_json_add_bool(item, "is_account_no_masked", false);
        }

        csilk_json_add_string(item, "symbol", csilk_json_get_string(r, "symbol"));
        csilk_json_add_string(item, "quote_source", csilk_json_get_string(r, "quote_source"));
        csilk_json_add_string(item, "last_sync_at", csilk_json_get_string(r, "last_sync_at"));

        int mask_bal = db_get_int(r, "mask_balance");
        if (mask_bal != 0) {
            csilk_json_add_number(item, "current_value", 0.0);
            csilk_json_add_number(item, "quantity", 0.0);
            csilk_json_add_number(item, "cost_basis", 0.0);
            csilk_json_add_number(item, "net_value", 0.0);
            csilk_json_add_bool(item, "is_balance_masked", true);
        } else {
            csilk_json_add_number(item, "current_value", db_get_num(r, "current_value"));
            csilk_json_add_number(item, "quantity", db_get_num(r, "quantity"));
            csilk_json_add_number(item, "cost_basis", db_get_num(r, "cost_basis"));
            csilk_json_add_number(item, "net_value", db_get_num(r, "net_value"));
            csilk_json_add_bool(item, "is_balance_masked", false);
        }

        csilk_json_add_string(item, "currency", csilk_json_get_string(r, "currency"));
        csilk_json_add_string(item, "note", csilk_json_get_string(r, "note"));
        csilk_json_add_string(item, "created_at", csilk_json_get_string(r, "created_at"));
        csilk_json_add_string(item, "updated_at", csilk_json_get_string(r, "updated_at"));
        const csilk_json_t* acct_v = csilk_json_get(r, "account_id");
        if (acct_v && !csilk_json_is_null(acct_v)) {
            csilk_json_add_int(item, "account_id", db_get_int(r, "account_id"));
        } else {
            csilk_json_add_null(item, "account_id");
        }
        csilk_json_add_string(item, "account_name", csilk_json_get_string(r, "account_name"));
        csilk_json_add_string(item, "institution", csilk_json_get_string(r, "institution"));
        csilk_json_add_string(item, "category_name", csilk_json_get_string(r, "category_name"));
        csilk_json_add_string(item, "asset_type", csilk_json_get_string(r, "asset_type"));
        csilk_json_add_string(item, "cost_method", csilk_json_get_string(r, "cost_method"));

        csilk_json_array_append(out, item);
    }
    csilk_json_free(raw_rows);
    return out;
}

static csilk_json_t*
format_balance_log_rows(csilk_json_t* raw_rows)
{
    if (!raw_rows || !csilk_json_is_array(raw_rows)) {
        return raw_rows;
    }
    size_t        n = csilk_json_array_size(raw_rows);
    csilk_json_t* out = csilk_json_array();
    if (!out) {
        return raw_rows;
    }
    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* r = csilk_json_array_get(raw_rows, i);
        if (!r || !csilk_json_is_object(r)) {
            continue;
        }
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_int(item, "id", db_get_int(r, "id"));
        csilk_json_add_int(item, "asset_id", db_get_int(r, "asset_id"));
        csilk_json_add_string(item, "asset_name", csilk_json_get_string(r, "asset_name"));
        csilk_json_add_int(item, "user_id", db_get_int(r, "user_id"));

        int mask_bal = db_get_int(r, "mask_balance");
        if (mask_bal != 0) {
            csilk_json_add_number(item, "delta", 0.0);
            csilk_json_add_number(item, "balance_after", 0.0);
            csilk_json_add_bool(item, "is_balance_masked", true);
        } else {
            csilk_json_add_number(item, "delta", db_get_num(r, "delta"));
            csilk_json_add_number(item, "balance_after", db_get_num(r, "balance_after"));
            csilk_json_add_bool(item, "is_balance_masked", false);
        }
        csilk_json_add_string(item, "source_type", csilk_json_get_string(r, "source_type"));
        csilk_json_add_int(item, "source_id", db_get_int(r, "source_id"));
        csilk_json_add_string(item, "note", csilk_json_get_string(r, "note"));
        csilk_json_add_string(item, "created_at", csilk_json_get_string(r, "created_at"));
        csilk_json_array_append(out, item);
    }
    csilk_json_free(raw_rows);
    return out;
}

/* --- SQL statements inlined from repositories/asset_repo.c --- */

csilk_json_t*
asset_list(csilk_db_pool_t* pool,
           int64_t          user_id,
           int64_t          caller_user_id,
           int64_t          ledger_id,
           int64_t          page,
           int64_t          page_size,
           const char*      category_id,
           int64_t          account_id,
           int64_t*         total)
{
    char uid[32], caller_uid[32], limit[32], offset[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(caller_uid, sizeof(caller_uid), "%lld", (long long)caller_user_id);
    snprintf(limit, sizeof(limit), "%lld", (long long)page_size);
    snprintf(offset, sizeof(offset), "%lld", (long long)((page - 1) * page_size));
    char        sql[2048], count_sql[1536];
    const char *params[24], *cnt_params[16];
    int         pidx = 0, cnt_pidx = 0;
    char        acct_str[32];
    int         has_cat = category_id && category_id[0];
    int         has_acct = account_id > 0;
    if (has_acct) {
        snprintf(acct_str, sizeof(acct_str), "%lld", (long long)account_id);
    }

    const char* ruid = (caller_user_id > 0) ? caller_uid : uid;
    /* SELECT 中 2 个脱敏列共 4 个参数，均对调用方(caller)求值 */
    params[pidx++] = ruid;
    params[pidx++] = ruid;
    params[pidx++] = ruid;
    params[pidx++] = ruid;

    char where_scope[256];
    if (ledger_id > 0) {
        snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
        snprintf(where_scope,
                 sizeof(where_scope),
                 "a.ledger_id=? AND (EXISTS (SELECT 1 FROM ledger_members lm WHERE "
                 "lm.ledger_id=a.ledger_id AND lm.user_id=?))");
        params[pidx++] = lid;
        params[pidx++] = ruid;
        cnt_params[cnt_pidx++] = lid;
        cnt_params[cnt_pidx++] = ruid;
    } else {
        snprintf(where_scope, sizeof(where_scope), "a.user_id=?");
        params[pidx++] = uid;
        cnt_params[cnt_pidx++] = uid;
    }

    /* MF_RBAC_ASSET_VISIBLE_SQL("a")：2 个 caller 参数 */
    params[pidx++] = ruid;
    params[pidx++] = ruid;
    cnt_params[cnt_pidx++] = ruid;
    cnt_params[cnt_pidx++] = ruid;

    if (has_cat) {
        params[pidx++] = category_id;
        cnt_params[cnt_pidx++] = category_id;
    }
    if (has_acct) {
        params[pidx++] = acct_str;
        cnt_params[cnt_pidx++] = acct_str;
    }
    params[pidx++] = limit;
    params[pidx++] = offset;
    params[pidx] = NULL;
    cnt_params[cnt_pidx] = NULL;

    {
        char buf[2048];
        int  off = 0;
        off += snprintf(
            buf + off,
            sizeof(buf) - off,
            "SELECT a.id,a.ledger_id,a.category_id,a.name,a.account_no,a.symbol,a.quote_source,"
            "CAST(a.last_sync_at AS TEXT) as last_sync_at,"
            "a.current_value,a.currency,a.note,a.created_at,a.updated_at,"
            "a.account_id,ac.name as account_name,ac.institution,"
            "c.name as category_name,c.asset_type,"
            "a.quantity,a.cost_basis,a.net_value,a.cost_"
            "method," MF_RBAC_MASK_BALANCE_COL(
                "a") " AS "
                     "mask_"
                     "balance"
                     "," MF_RBAC_MASK_ACCOUNT_NO_COL("a") " AS mask_account_no "
                                                          "FROM assets a "
                                                          "LEFT JOIN categories c "
                                                          "ON a.category_id=c.id "
                                                          "LEFT JOIN accounts ac ON "
                                                          "a.account_id=ac.id "
                                                          "WHERE %s AND " MF_RBAC_ASSET_VISIBLE_SQL(
                                                              "a"),
            where_scope);
        if (has_cat) {
            off += snprintf(buf + off, sizeof(buf) - off, " AND a.category_id=?");
        }
        if (has_acct) {
            off += snprintf(buf + off, sizeof(buf) - off, " AND a.account_id=?");
        }
        off += snprintf(buf + off, sizeof(buf) - off, " ORDER BY c.name,a.name LIMIT ? OFFSET ?");
        snprintf(sql, sizeof(sql), "%s", buf);
    }
    {
        char cbuf[1536];
        int  coff = 0;
        coff += snprintf(cbuf + coff,
                         sizeof(cbuf) - coff,
                         "SELECT COUNT(*) AS cnt FROM assets a "
                         "WHERE %s AND " MF_RBAC_ASSET_VISIBLE_SQL("a"),
                         where_scope);
        if (has_cat) {
            coff += snprintf(cbuf + coff, sizeof(cbuf) - coff, " AND a.category_id=?");
        }
        if (has_acct) {
            coff += snprintf(cbuf + coff, sizeof(cbuf) - coff, " AND a.account_id=?");
        }
        snprintf(count_sql, sizeof(count_sql), "%s", cbuf);
    }

    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    *total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }
    csilk_json_t* raw_rows = csilk_db_query_param_json(pool, sql, params);
    return raw_rows ? format_asset_rows(raw_rows, false) : NULL;
}

csilk_json_t*
asset_get_raw(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    return csilk_db_query_param_json(
        pool,
        "SELECT "
        "a.id,a.ledger_id,a.category_id,a.name,a.account_no,a.symbol,a.quote_source,CAST(a.last_"
        "sync_at AS "
        "TEXT) as last_sync_at,"
        "a.current_value,a.currency,a.note,a.created_at,a."
        "updated_at,a.account_id,ac.name AS account_name,ac.institution,"
        "c.name as category_name,c.asset_type,a.quantity,a.cost_basis,a.net_value,a.cost_method "
        "FROM "
        "assets a LEFT JOIN categories c ON a.category_id=c.id "
        "LEFT JOIN accounts ac ON a.account_id=ac.id WHERE a.id=? AND a.user_id=?",
        (const char*[]){idstr, uid, NULL});
}

csilk_json_t*
asset_get(csilk_db_pool_t* pool, int64_t user_id, int64_t caller_user_id, int64_t id)
{
    char uid[32], caller_uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(caller_uid, sizeof(caller_uid), "%lld", (long long)caller_user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);

    const char* ruid = (caller_user_id > 0) ? caller_uid : uid;

    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT "
        "a.id,a.ledger_id,a.category_id,a.name,a.account_no,a.symbol,a.quote_source,CAST(a.last_"
        "sync_at AS TEXT) as last_sync_at,"
        "a.current_value,a.currency,a.note,a.created_at,a.updated_at,a.account_id,ac.name AS "
        "account_name,ac.institution,"
        "c.name as "
        "category_name,c.asset_type,a.quantity,a.cost_basis,a.net_value,a.cost_"
        "method," MF_RBAC_MASK_BALANCE_COL("a") " AS mask_balance," MF_RBAC_MASK_ACCOUNT_NO_COL(
            "a") " AS mask_account_no "
                 "FROM assets a "
                 "LEFT JOIN categories c ON a.category_id=c.id "
                 "LEFT JOIN accounts ac ON a.account_id=ac.id "
                 "WHERE a.id=? AND ( "
                 "  a.user_id=? OR (a.ledger_id > 0 AND EXISTS (SELECT 1 FROM ledger_members lm "
                 "WHERE lm.ledger_id=a.ledger_id AND lm.user_id=?)) "
                 ") AND " MF_RBAC_ASSET_VISIBLE_SQL("a"),
        (const char*[]){ruid, ruid, ruid, ruid, idstr, uid, ruid, ruid, ruid, NULL});
    return rows ? format_asset_rows(rows, true) : NULL;
}

int64_t
asset_insert(csilk_db_pool_t* pool,
             int64_t          user_id,
             int64_t          category_id,
             int64_t          account_id,
             const char*      name,
             const char*      account_no,
             double           current_value,
             const char*      currency,
             const char*      note,
             double           quantity,
             double           cost_basis,
             double           net_value,
             const char*      symbol,
             const char*      quote_source,
             const char*      cost_method)
{
    char uid[32], cat[32], val[64], qty[64], cb[64], nv[64], acct[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(acct, sizeof(acct), "%lld", (long long)account_id);
    snprintf(val, sizeof(val), "%.8f", current_value);
    snprintf(qty, sizeof(qty), "%.8f", quantity);
    snprintf(cb, sizeof(cb), "%.8f", cost_basis);
    snprintf(nv, sizeof(nv), "%.8f", net_value);
    const char* method = (cost_method && cost_method[0]) ? cost_method : "weighted_average";

    /* account_id > 0：真实账户；account_id <= 0 时写 NULL（避免 FK RESTRICT 违规） */
    char        ins_sql[512];
    const char* ins_params[20];
    int         nparams = 0;
    ins_params[nparams++] = uid;
    ins_params[nparams++] = cat;
    ins_params[nparams++] = name;
    ins_params[nparams++] = account_no ? account_no : "";
    ins_params[nparams++] = val;
    ins_params[nparams++] = currency ? currency : "CNY";
    ins_params[nparams++] = note ? note : "";
    ins_params[nparams++] = qty;
    ins_params[nparams++] = cb;
    ins_params[nparams++] = nv;
    ins_params[nparams++] = symbol ? symbol : "";
    ins_params[nparams++] = quote_source ? quote_source : "";
    ins_params[nparams++] = method;
    if (account_id > 0) {
        ins_params[nparams++] = acct;
    }
    ins_params[nparams++] = cat;
    ins_params[nparams++] = uid;
    if (account_id > 0) {
        ins_params[nparams++] = acct;
    }
    ins_params[nparams] = NULL;

    snprintf(ins_sql,
             sizeof(ins_sql),
             "INSERT INTO assets "
             "(user_id,category_id,name,account_no,current_value,currency,note,quantity,cost_"
             "basis,net_value,symbol,quote_source,cost_method,ledger_id,account_id) VALUES "
             "(?,?,?,?,?,?,?,?,?,?,?,?,?,"
             "COALESCE(%s(SELECT ledger_id FROM categories WHERE id=?), (SELECT id FROM ledgers "
             "WHERE owner_id=? AND is_default=1)),%s) RETURNING id",
             account_id > 0 ? "(SELECT ledger_id FROM accounts WHERE id=?), " : "",
             account_id > 0 ? "?" : "NULL");

    csilk_json_t* res = csilk_db_query_param_json(pool, ins_sql, ins_params);
    int64_t       id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }

    /* Record initial balance baseline into asset_balance_logs */
    if (id > 0 && fabs(current_value) > 0.000001) {
        char aid[32];
        snprintf(aid, sizeof(aid), "%lld", (long long)id);
        const char*   log_params[] = {aid, uid, val, val, "init", "0", "Initial balance", NULL};
        csilk_json_t* log_res =
            csilk_db_query_param_json(pool,
                                      "INSERT INTO asset_balance_logs (asset_id, user_id, delta, "
                                      "balance_after, source_type, source_id, note) "
                                      "VALUES (?, ?, ?, ?, ?, ?, ?)",
                                      log_params);
        if (log_res) {
            csilk_json_free(log_res);
        }
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

    /* Fetch previous balance before update to detect manual balance adjustments */
    double        prev_val = 0.0;
    csilk_json_t* prev_row =
        csilk_db_query_param_json(pool,
                                  "SELECT current_value FROM assets WHERE id=? AND user_id=?",
                                  (const char*[]){idstr, uid, NULL});
    if (prev_row && csilk_json_array_size(prev_row) > 0) {
        prev_val = db_get_num(csilk_json_array_get(prev_row, 0), "current_value");
    }
    if (prev_row) {
        csilk_json_free(prev_row);
    }

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

    /* Record manual balance delta adjustment if value changed */
    if (ok && fabs(current_value - prev_val) > 0.000001) {
        char delta_str[64];
        snprintf(delta_str, sizeof(delta_str), "%.6f", current_value - prev_val);
        const char* adj_params[] = {
            idstr, uid, delta_str, val, "adjustment", "0", "Manual adjustment", NULL};
        csilk_json_t* adj_res =
            csilk_db_query_param_json(pool,
                                      "INSERT INTO asset_balance_logs (asset_id, user_id, delta, "
                                      "balance_after, source_type, source_id, note) "
                                      "VALUES (?, ?, ?, ?, ?, ?, ?)",
                                      adj_params);
        if (adj_res) {
            csilk_json_free(adj_res);
        }
    }

    return ok;
}

int
asset_update_account(csilk_db_pool_t* pool, int64_t user_id, int64_t id, int64_t account_id)
{
    char uid[32], idstr[32], acct[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);

    char        sql[512];
    const char* params[5] = {NULL, NULL, NULL, NULL, NULL};
    if (account_id > 0) {
        snprintf(acct, sizeof(acct), "%lld", (long long)account_id);
        snprintf(sql,
                 sizeof(sql),
                 "UPDATE assets SET account_id=?, ledger_id=COALESCE((SELECT ledger_id FROM "
                 "accounts WHERE id=?), ledger_id), updated_at=CURRENT_TIMESTAMP WHERE id=? AND "
                 "user_id=? RETURNING id");
        params[0] = acct;
        params[1] = acct;
        params[2] = idstr;
        params[3] = uid;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "UPDATE assets SET account_id=NULL, updated_at=CURRENT_TIMESTAMP WHERE id=? AND "
                 "user_id=? RETURNING id");
        params[0] = idstr;
        params[1] = uid;
    }

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : 1;
}

int
asset_update_cost_method(csilk_db_pool_t* pool,
                         int64_t          user_id,
                         int64_t          id,
                         const char*      cost_method)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    const char* method = (cost_method && cost_method[0]) ? cost_method : "weighted_average";

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "UPDATE assets SET cost_method=?, updated_at=CURRENT_TIMESTAMP "
                                  "WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){method, idstr, uid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : 1;
}

int
asset_update_market_quote(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          int64_t          asset_id,
                          double           new_net_value)
{
    char val[64], aid[32], uid[32];
    snprintf(val, sizeof(val), "%.8f", new_net_value);
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
            "currency, cost_method "
            "FROM assets WHERE user_id=? AND symbol IS NOT NULL AND symbol != ''",
            (const char*[]){uid, NULL});
    } else {
        return csilk_db_query_json(pool,
                                   "SELECT id, user_id, category_id, name, symbol, quote_source, "
                                   "COALESCE(CAST(net_value AS REAL), 0.0) as net_value, "
                                   "COALESCE(CAST(quantity AS REAL), 0.0) as quantity, "
                                   "currency, cost_method "
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
    snprintf(nv, sizeof(nv), "%.8f", net_value);
    snprintf(qty, sizeof(qty), "%.8f", quantity);
    snprintf(cb, sizeof(cb), "%.8f", cost_basis);
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
asset_has_transactions(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, int* out_flag)
{
    if (!pool || user_id <= 0 || asset_id <= 0 || !out_flag) {
        return -1;
    }
    char uid[32], aid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT 1 AS hit FROM transactions "
                                  "WHERE user_id=? AND (asset_id=? OR linked_asset_id=?) LIMIT 1",
                                  (const char*[]){uid, aid, aid, NULL});
    *out_flag = (res && csilk_json_array_size(res) > 0) ? 1 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

int
asset_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);

    csilk_json_free(csilk_db_query_param_json(
        pool,
        "DELETE FROM transfers WHERE (from_asset_id=? OR to_asset_id=?) AND user_id=?",
        (const char*[]){idstr, idstr, uid, NULL}));

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
asset_daily_balance_log_sum(csilk_db_pool_t* pool,
                            int64_t          user_id,
                            int64_t          asset_id,
                            const char*      start_date,
                            const char*      end_date,
                            double*          out_sum)
{
    if (!pool || user_id <= 0 || asset_id <= 0 || !out_sum) {
        return -1;
    }
    *out_sum = 0.0;

    char uid_str[32], aid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);

    /* 负债类资产方向翻转（与 balance_apply_delta 一致）：
     * 负债增加 → 净资产减少，所以 SUM(delta) 对负债取负 */
    const char* sql = "SELECT c.asset_type, COALESCE(SUM(abl.delta), 0.0) as total_delta "
                      "FROM asset_balance_logs abl "
                      "JOIN assets a ON abl.asset_id = a.id "
                      "JOIN categories c ON a.category_id = c.id "
                      "WHERE abl.user_id = ? AND abl.asset_id = ? "
                      "AND date(abl.created_at) >= ? AND date(abl.created_at) <= ? "
                      "GROUP BY c.asset_type";

    csilk_json_t* rows =
        csilk_db_query_param_json(pool,
                                  sql,
                                  (const char*[]){uid_str,
                                                  aid_str,
                                                  start_date ? start_date : "1970-01-01",
                                                  end_date ? end_date : "9999-12-31",
                                                  NULL});

    if (!rows) {
        return -1;
    }

    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* row = csilk_json_array_get(rows, i);
        double              delta = db_get_num(row, "total_delta");
        const char*         atype = csilk_json_get_string(row, "asset_type");
        int is_liab = (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                                 strcmp(atype, "other_liability") == 0));
        if (is_liab) {
            delta = -delta;
        }
        *out_sum += delta;
    }

    csilk_json_free(rows);
    return 0;
}

int
asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT a.id FROM assets a WHERE a.id=? AND (a.user_id=? OR (a.ledger_id > 0 AND EXISTS "
        "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = a.ledger_id AND lm.user_id=?)))",
        (const char*[]){idstr, uid, uid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int64_t
asset_get_account_id(csilk_db_pool_t* pool, int64_t asset_id)
{
    if (!pool || asset_id <= 0) {
        return 0;
    }
    char aid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT account_id FROM assets WHERE id=?", (const char*[]){aid_str, NULL});
    int64_t acct_id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        acct_id = db_get_int(csilk_json_array_get(res, 0), "account_id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return acct_id;
}

int
asset_get_ledger_and_account(csilk_db_pool_t* pool,
                             int64_t          asset_id,
                             int64_t*         out_ledger_id,
                             int64_t*         out_account_id)
{
    if (out_ledger_id) {
        *out_ledger_id = 0;
    }
    if (out_account_id) {
        *out_account_id = 0;
    }
    if (!pool || asset_id <= 0) {
        return -1;
    }
    char aid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(ac.ledger_id, a.ledger_id) AS ledger_id, a.account_id "
        "FROM assets a LEFT JOIN accounts ac ON a.account_id = ac.id "
        "WHERE a.id=?",
        (const char*[]){aid_str, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }
    const csilk_json_t* r = csilk_json_array_get(res, 0);
    if (out_ledger_id) {
        *out_ledger_id = db_get_int(r, "ledger_id");
    }
    if (out_account_id) {
        *out_account_id = db_get_int(r, "account_id");
    }
    csilk_json_free(res);
    return 0;
}

char*
asset_get_category_type(csilk_db_pool_t* pool, int64_t user_id, int64_t category_id)
{
    char uid[32], cat[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT c.asset_type FROM categories c WHERE c.id=? AND (c.user_id=? OR (c.ledger_id > 0 "
        "AND EXISTS "
        "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = c.ledger_id AND lm.user_id=?)))",
        (const char*[]){cat, uid, uid, NULL});
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
asset_transactions(csilk_db_pool_t* pool, int64_t user_id, int64_t caller_user_id, int64_t asset_id)
{
    char uid[32], caller_uid[32], aid[32];
    int  member_view = (caller_user_id > 0 && caller_user_id != user_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(caller_uid, sizeof(caller_uid), "%lld", (long long)caller_user_id);
    snprintf(aid, sizeof(aid), "%lld", (long long)asset_id);
    /* 成员视图：与资产一致使用 IN(owner, caller) 门控交易行，避免越权读 owner 交易。 */
    if (!member_view) {
        return csilk_db_query_param_json(
            pool,
            "SELECT "
            "t.id,t.asset_id,t.transaction_type,t.amount,t.quantity,t.price_per_unit,t.currency,"
            "t.transaction_date,t.note,t.created_at FROM transactions t "
            "WHERE t.asset_id=? AND (t.user_id=? OR EXISTS ("
            "  SELECT 1 FROM assets a JOIN ledger_members lm ON lm.ledger_id=a.ledger_id "
            "  WHERE a.id=t.asset_id AND lm.user_id=?"
            ")) ORDER BY t.transaction_date DESC",
            (const char*[]){aid, uid, uid, NULL});
    }
    return csilk_db_query_param_json(
        pool,
        "SELECT "
        "id,asset_id,transaction_type,amount,quantity,price_per_unit,currency,transaction_date,"
        "note,created_at FROM transactions WHERE asset_id=? AND user_id IN (?,?) ORDER BY "
        "transaction_date DESC",
        (const char*[]){aid, uid, caller_uid, NULL});
}

csilk_json_t*
asset_balance_logs_list(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          caller_user_id,
                        int64_t          page,
                        int64_t          page_size,
                        const char*      asset_id_str,
                        int64_t*         total)
{
    char uid_str[32], caller_str[32], limit_buf[32], offset_buf[32], aid_buf[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(caller_str, sizeof(caller_str), "%lld", (long long)caller_user_id);
    snprintf(limit_buf, sizeof(limit_buf), "%lld", (long long)page_size);
    snprintf(offset_buf, sizeof(offset_buf), "%lld", (long long)((page - 1) * page_size));

    int         member_view = (caller_user_id > 0 && caller_user_id != user_id);
    const char* ruid = member_view ? caller_str : uid_str;
    /* SELECT 中 1 个脱敏列共 2 个参数 + WHERE abl.user_id(?) + visible(2) 共 5 个 */
    int has_asset = asset_id_str && strlen(asset_id_str) > 0;
    if (has_asset) {
        snprintf(aid_buf, sizeof(aid_buf), "%lld", atoll(asset_id_str));
    }

    char        data_sql[1536], count_sql[1024];
    const char *params[16], *cnt_params[12];
    int         pidx = 0, cnt_pidx = 0;

    /* data SQL: mask(2 ruid) + abl.user_id/ledger member(2) + visible(2 ruid) + asset? + limit/offset */
    params[pidx++] = ruid;
    params[pidx++] = ruid;
    params[pidx++] = uid_str;
    params[pidx++] = ruid;
    params[pidx++] = ruid;
    params[pidx++] = ruid;
    if (has_asset) {
        params[pidx++] = aid_buf;
    }
    params[pidx++] = limit_buf;
    params[pidx++] = offset_buf;
    params[pidx] = NULL;

    /* count SQL: abl.user_id/ledger member(2) + visible(2 ruid) + asset? */
    cnt_params[cnt_pidx++] = uid_str;
    cnt_params[cnt_pidx++] = ruid;
    cnt_params[cnt_pidx++] = ruid;
    cnt_params[cnt_pidx++] = ruid;
    if (has_asset) {
        cnt_params[cnt_pidx++] = aid_buf;
    }
    cnt_params[cnt_pidx] = NULL;

    int off = 0;
    off += snprintf(data_sql + off,
                    sizeof(data_sql) - off,
                    "SELECT abl.id, abl.asset_id, a.name AS asset_name, abl.user_id, "
                    "abl.delta, abl.balance_after, abl.source_type, abl.source_id, "
                    "abl.note, abl.created_at, " MF_RBAC_MASK_BALANCE_COL(
                        "a") " AS mask_balance "
                             "FROM asset_balance_logs abl "
                             "LEFT JOIN assets a ON abl.asset_id = a.id "
                             "WHERE (abl.user_id=? OR (a.ledger_id > 0 AND EXISTS "
                             "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = a.ledger_id "
                             "AND lm.user_id = ?))) "
                             "AND " MF_RBAC_ASSET_VISIBLE_SQL("a"));
    if (has_asset) {
        off += snprintf(data_sql + off, sizeof(data_sql) - off, " AND abl.asset_id=?");
    }
    off += snprintf(
        data_sql + off, sizeof(data_sql) - off, " ORDER BY abl.created_at DESC LIMIT ? OFFSET ?");

    int coff = 0;
    coff += snprintf(
        count_sql + coff,
        sizeof(count_sql) - coff,
        "SELECT COUNT(*) AS cnt FROM asset_balance_logs abl "
        "LEFT JOIN assets a ON abl.asset_id = a.id "
        "WHERE (abl.user_id=? OR (a.ledger_id > 0 AND EXISTS "
        "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = a.ledger_id AND lm.user_id = ?))) "
        "AND " MF_RBAC_ASSET_VISIBLE_SQL("a"));
    if (has_asset) {
        coff += snprintf(count_sql + coff, sizeof(count_sql) - coff, " AND abl.asset_id=?");
    }

    csilk_json_t* result = csilk_db_query_param_json(pool, data_sql, params);
    *total = 0;
    if (result == NULL) {
        return NULL;
    }

    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }

    return format_balance_log_rows(result);
}

/* --- Domain repository implementation (mf_asset_repo_*) --- */

int
mf_asset_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_asset_t* out_asset)
{
    if (!out_asset || !db_pool) {
        return -1;
    }
    memset(out_asset, 0, sizeof(*out_asset));
    csilk_json_t* res = asset_get_raw((csilk_db_pool_t*)db_pool, user_id, id);
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
    out_asset->account_id = db_get_int(r, "account_id");
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
    s = csilk_json_get_string(r, "cost_method");
    if (s && s[0]) {
        snprintf(out_asset->cost_method, sizeof(out_asset->cost_method), "%s", s);
    } else {
        snprintf(out_asset->cost_method, sizeof(out_asset->cost_method), "weighted_average");
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
                              asset->account_id,
                              asset->name,
                              asset->account_no,
                              money_to_double(asset->current_value),
                              currency_code(&asset->currency),
                              asset->note,
                              quantity_to_double(asset->quantity),
                              money_to_double(asset->cost_basis),
                              price_to_double(asset->net_value),
                              asset->symbol,
                              asset->quote_source,
                              asset->cost_method);
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
    return asset_delete((csilk_db_pool_t*)db_pool, user_id, id) ? 0 : -1;
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

int
asset_count_transactions(void* pool, int64_t user_id, int64_t asset_id)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return 0;
    }
    char uid_str[32], aid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* tx_check = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT COUNT(*) as cnt FROM transactions WHERE user_id=? AND asset_id=?",
        (const char*[]){uid_str, aid_str, NULL});
    int64_t tx_cnt = 0;
    if (tx_check && csilk_json_array_size(tx_check) > 0) {
        tx_cnt = db_get_int(csilk_json_array_get(tx_check, 0), "cnt");
    }
    if (tx_check) {
        csilk_json_free(tx_check);
    }
    return (int)tx_cnt;
}

int64_t
asset_get_ledger_id(void* pool, int64_t user_id, int64_t asset_id)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return 0;
    }
    char aid_buf[32], uid_buf[32];
    snprintf(aid_buf, sizeof(aid_buf), "%lld", (long long)asset_id);
    snprintf(uid_buf, sizeof(uid_buf), "%lld", (long long)user_id);
    int64_t       asset_ledger = 0;
    csilk_json_t* lid_res =
        csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                  "SELECT ledger_id FROM assets WHERE id=? AND user_id=?",
                                  (const char*[]){aid_buf, uid_buf, NULL});
    if (lid_res && csilk_json_array_size(lid_res) > 0) {
        asset_ledger = db_get_int(csilk_json_array_get(lid_res, 0), "ledger_id");
    }
    if (lid_res) {
        csilk_json_free(lid_res);
    }
    return asset_ledger;
}

csilk_json_t*
asset_list_values_with_type(void* pool, int64_t user_id, int64_t ledger_id)
{
    if (!pool || user_id <= 0) {
        return NULL;
    }
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    if (ledger_id > 0) {
        snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
        return csilk_db_query_param_json(
            (csilk_db_pool_t*)pool,
            "SELECT a.current_value, a.currency, c.asset_type FROM assets a "
            "JOIN categories c ON a.category_id=c.id "
            "WHERE a.ledger_id=? AND (EXISTS (SELECT 1 FROM ledger_members lm WHERE "
            "lm.ledger_id=a.ledger_id AND lm.user_id=?)) "
            "AND c.asset_type NOT IN ('loan','credit_card','other_liability') "
            "AND " MF_RBAC_ASSET_UNMASKED_SQL("a"),
            (const char*[]){lid_str, uid_str, uid_str, uid_str, NULL});
    }
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT a.current_value, a.currency, c.asset_type FROM assets a "
        "JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type NOT IN "
        "('loan','credit_card','other_liability') AND " MF_RBAC_ASSET_UNMASKED_SQL("a"),
        (const char*[]){uid_str, uid_str, uid_str, NULL});
}

int
asset_get_currency(void* pool, int64_t asset_id, char* out_currency, size_t max_len)
{
    if (!pool || asset_id <= 0 || !out_currency || max_len == 0) {
        return -1;
    }
    char aid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* ares = csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                                   "SELECT currency FROM assets WHERE id=?",
                                                   (const char*[]){aid_str, NULL});
    int           rc = -1;
    if (ares && csilk_json_array_size(ares) > 0) {
        const char* cur = csilk_json_get_string(csilk_json_array_get(ares, 0), "currency");
        if (cur && cur[0]) {
            snprintf(out_currency, max_len, "%s", cur);
            rc = 0;
        }
    }
    if (ares) {
        csilk_json_free(ares);
    }
    return rc;
}
