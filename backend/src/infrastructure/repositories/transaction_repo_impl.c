#include "infrastructure/repositories/transaction_repo_impl.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "common/db.h"
#include "core/financial/currency.h"
#include "core/financial/quantity.h"
#include "core/financial/price.h"
#include "core/financial/money.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
mf_tx_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_transaction_t* out_tx)
{
    if (!db_pool || user_id <= 0 || id <= 0 || !out_tx) {
        return -1;
    }
    memset(out_tx, 0, sizeof(*out_tx));

    char uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT id, user_id, asset_id, linked_asset_id, parent_tx_id, transaction_type, "
        "amount, price_per_unit, quantity, fee, currency, note, transaction_date, created_at "
        "FROM transactions WHERE id=? AND (user_id=? OR (ledger_id > 0 AND EXISTS "
        "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id=transactions.ledger_id AND "
        "lm.user_id=?)))",
        (const char*[]){id_str, uid_str, uid_str, NULL});

    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1; /* Not found */
    }

    csilk_json_t* row = csilk_json_array_get(res, 0);
    out_tx->id = db_get_int(row, "id");
    out_tx->user_id = db_get_int(row, "user_id");
    out_tx->asset_id = db_get_int(row, "asset_id");
    out_tx->account_id = db_get_int(row, "linked_asset_id");
    out_tx->parent_tx_id = db_get_int(row, "parent_tx_id");

    const char* type = csilk_json_get_string(row, "transaction_type");
    if (type) {
        snprintf(out_tx->type, sizeof(out_tx->type), "%s", type);
    }

    const char* cur_code = csilk_json_get_string(row, "currency");
    if (!cur_code) {
        cur_code = "CNY";
    }
    snprintf(out_tx->fee_currency, sizeof(out_tx->fee_currency), "%s", cur_code);
    currency_t cur = currency_from_str(cur_code);

    quantity_t q = db_get_quantity(row, "quantity");
    if (quantity_is_positive(q)) {
        out_tx->amount = q;
    } else {
        out_tx->amount = db_get_quantity(row, "amount");
    }
    out_tx->price = db_get_price(row, "price_per_unit", cur);
    out_tx->fee = db_get_money(row, "fee", cur);

    const char* note = csilk_json_get_string(row, "note");
    if (note) {
        snprintf(out_tx->note, sizeof(out_tx->note), "%s", note);
    }

    const char* tx_date = csilk_json_get_string(row, "transaction_date");
    if (tx_date) {
        snprintf(out_tx->tx_time, sizeof(out_tx->tx_time), "%s", tx_date);
    }

    const char* cat = csilk_json_get_string(row, "created_at");
    if (cat) {
        snprintf(out_tx->created_at, sizeof(out_tx->created_at), "%s", cat);
    }

    const char* uat = csilk_json_get_string(row, "updated_at");
    if (uat) {
        snprintf(out_tx->updated_at, sizeof(out_tx->updated_at), "%s", uat);
    }

    csilk_json_free(res);
    return 0;
}

static void
format_tx_amount_and_qty(
    const mf_transaction_t* tx, char* amt_str, size_t amt_sz, char* qty_str, size_t qty_sz)
{
    quantity_to_string_fixed(tx->amount, 8, qty_str, qty_sz);
    money_t total_m;
    if (decimal_is_positive(tx->price.unit_price) && quantity_is_positive(tx->amount) &&
        price_times_quantity(tx->price, tx->amount, &total_m) == DECIMAL_OK) {
        money_to_string(total_m, amt_str, amt_sz);
    } else {
        quantity_to_string_fixed(tx->amount, 8, amt_str, amt_sz);
    }
}

int
mf_tx_repo_save(void* db_pool, const mf_transaction_t* tx, int64_t* out_id)
{
    if (!db_pool || !tx) {
        return -1;
    }

    char uid_str[32], asset_id_str[32], linked_asset_id_str[32], parent_id_str[32];
    char amt_str[64], price_str[64], qty_str[64], fee_str[64];

    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)tx->user_id);
    snprintf(asset_id_str, sizeof(asset_id_str), "%lld", (long long)tx->asset_id);
    snprintf(linked_asset_id_str, sizeof(linked_asset_id_str), "%lld", (long long)tx->account_id);
    snprintf(parent_id_str, sizeof(parent_id_str), "%lld", (long long)tx->parent_tx_id);

    format_tx_amount_and_qty(tx, amt_str, sizeof(amt_str), qty_str, sizeof(qty_str));
    price_to_string_fixed(tx->price, 8, price_str, sizeof(price_str));
    money_to_string(tx->fee, fee_str, sizeof(fee_str));

    const char* currency = tx->fee_currency[0] ? tx->fee_currency : "CNY";
    const char* date = tx->tx_time[0] ? tx->tx_time : "2026-01-01";
    const char* note = tx->note;

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "INSERT INTO transactions (user_id, asset_id, linked_asset_id, parent_tx_id, "
        "transaction_type, "
        "amount, price_per_unit, quantity, fee, currency, note, transaction_date, ledger_id) "
        "VALUES (?, ?, NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), NULLIF(CAST(NULLIF(?, '') AS "
        "BIGINT), 0), ?, ?, ?, ?, "
        "?, ?, ?, ?, "
        "COALESCE((SELECT ledger_id FROM assets WHERE id = ?), (SELECT id FROM ledgers WHERE "
        "owner_id = ? AND is_default = 1))) RETURNING id",
        (const char*[]){uid_str,
                        asset_id_str,
                        linked_asset_id_str,
                        parent_id_str,
                        tx->type,
                        amt_str,
                        price_str,
                        qty_str,
                        fee_str,
                        currency,
                        note,
                        date,
                        asset_id_str,
                        uid_str,
                        NULL});

    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }

    if (out_id) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        *out_id = db_get_int(row, "id");
    }

    csilk_json_free(res);
    return 0;
}

int
mf_tx_repo_update(void* db_pool, const mf_transaction_t* tx)
{
    if (!db_pool || !tx || tx->id <= 0) {
        return -1;
    }

    char id_str[32], uid_str[32], asset_id_str[32], linked_asset_id_str[32];
    char amt_str[64], price_str[64], qty_str[64], fee_str[64];

    snprintf(id_str, sizeof(id_str), "%lld", (long long)tx->id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)tx->user_id);
    snprintf(asset_id_str, sizeof(asset_id_str), "%lld", (long long)tx->asset_id);
    snprintf(linked_asset_id_str, sizeof(linked_asset_id_str), "%lld", (long long)tx->account_id);

    format_tx_amount_and_qty(tx, amt_str, sizeof(amt_str), qty_str, sizeof(qty_str));
    price_to_string_fixed(tx->price, 8, price_str, sizeof(price_str));
    money_to_string(tx->fee, fee_str, sizeof(fee_str));

    const char* currency = tx->fee_currency[0] ? tx->fee_currency : "CNY";
    const char* date = tx->tx_time[0] ? tx->tx_time : "2026-01-01";
    const char* note = tx->note;

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "UPDATE transactions SET asset_id=?, linked_asset_id=NULLIF(CAST(NULLIF(?, '') AS BIGINT), "
        "0), "
        "transaction_type=?, "
        "amount=?, price_per_unit=?, quantity=?, fee=?, currency=?, note=?, transaction_date=? "
        "WHERE id=? AND (user_id=? OR (ledger_id > 0 AND EXISTS "
        "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id=transactions.ledger_id AND "
        "lm.user_id=? AND lm.role IN ('owner','editor')))) RETURNING id",
        (const char*[]){asset_id_str,
                        linked_asset_id_str,
                        tx->type,
                        amt_str,
                        price_str,
                        qty_str,
                        fee_str,
                        currency,
                        note,
                        date,
                        id_str,
                        uid_str,
                        uid_str,
                        NULL});

    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : -1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_tx_repo_delete(void* db_pool, int64_t user_id, int64_t id)
{
    return tx_delete((csilk_db_pool_t*)db_pool, user_id, id) ? 0 : -1;
}

int
mf_tx_repo_find_fee_children(void*              db_pool,
                             int64_t            user_id,
                             int64_t            parent_tx_id,
                             mf_transaction_t** out_list,
                             size_t*            out_count)
{
    if (!db_pool || user_id <= 0 || parent_tx_id <= 0 || !out_list || !out_count) {
        return -1;
    }
    *out_list = NULL;
    *out_count = 0;

    csilk_json_t* rows = tx_child_fee_rows((csilk_db_pool_t*)db_pool, user_id, parent_tx_id);
    if (!rows) {
        return 0;
    }

    size_t count = (size_t)csilk_json_array_size(rows);
    if (count == 0) {
        csilk_json_free(rows);
        return 0;
    }

    mf_transaction_t* list = (mf_transaction_t*)calloc(count, sizeof(mf_transaction_t));
    if (!list) {
        csilk_json_free(rows);
        return -1;
    }

    currency_t cny = currency_from_str("CNY");
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        list[i].id = db_get_int(r, "id");
        list[i].user_id = user_id;
        list[i].parent_tx_id = parent_tx_id;
        list[i].account_id = db_get_int(r, "linked_asset_id");
        snprintf(list[i].type, sizeof(list[i].type), "fee");

        list[i].amount = db_get_quantity(r, "amount");
        list[i].fee = db_get_money(r, "amount", cny);
        list[i].price = db_get_price(r, "amount", cny);

        const char* note = csilk_json_get_string(r, "note");
        if (note) {
            snprintf(list[i].note, sizeof(list[i].note), "%s", note);
        }
    }

    csilk_json_free(rows);
    *out_list = list;
    *out_count = count;
    return 0;
}

int
mf_tx_repo_delete_fee_children(void* db_pool, int64_t user_id, int64_t parent_tx_id)
{
    return tx_delete_fee_children((csilk_db_pool_t*)db_pool, user_id, parent_tx_id) ? 0 : -1;
}

void
mf_tx_repo_free_list(mf_transaction_t* list, size_t count)
{
    (void)count;
    if (list) {
        free(list);
    }
}

/* --- Inlined from legacy transaction_repo.c --- */

int
tx_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "DELETE FROM transactions WHERE id=? AND (user_id=? OR (ledger_id > 0 AND EXISTS "
        "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id=transactions.ledger_id AND "
        "lm.user_id=? AND lm.role IN ('owner','editor')))) RETURNING id",
        (const char*[]){idstr, uid, uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
tx_asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    char uid[32], ast[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT a.id FROM assets a WHERE a.id=? AND (a.user_id=? OR (a.ledger_id > 0 AND EXISTS "
        "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = a.ledger_id AND lm.user_id=?)))",
        (const char*[]){ast, uid, uid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
tx_asset_get_type(
    csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, char* out_type, size_t cap)
{
    if (!pool || user_id <= 0 || asset_id <= 0 || !out_type || cap == 0) {
        return -1;
    }
    out_type[0] = '\0';
    char uid[32], ast[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(c.asset_type, '') AS asset_type "
        "FROM assets a LEFT JOIN categories c ON a.category_id = c.id "
        "WHERE a.id=? AND (a.user_id=? OR (a.ledger_id > 0 AND EXISTS "
        "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = a.ledger_id AND lm.user_id=?)))",
        (const char*[]){ast, uid, uid, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1;
    }
    const char* atype = csilk_json_get_string(csilk_json_array_get(res, 0), "asset_type");
    snprintf(out_type, cap, "%s", atype ? atype : "");
    csilk_json_free(res);
    return 0;
}

/* --- Inlined: tx_list, tx_insert, tx_monthly --- */

static csilk_json_t*
format_tx_rows(csilk_json_t* raw_rows)
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
        const csilk_json_t* laid_v = csilk_json_get(r, "linked_asset_id");
        if (laid_v && !csilk_json_is_null(laid_v)) {
            csilk_json_add_int(item, "linked_asset_id", db_get_int(r, "linked_asset_id"));
        } else {
            csilk_json_add_null(item, "linked_asset_id");
        }
        const csilk_json_t* cat_v = csilk_json_get(r, "category_id");
        if (cat_v && !csilk_json_is_null(cat_v)) {
            csilk_json_add_int(item, "category_id", db_get_int(r, "category_id"));
        } else {
            csilk_json_add_null(item, "category_id");
        }
        csilk_json_add_string(
            item, "transaction_type", csilk_json_get_string(r, "transaction_type"));
        csilk_json_add_string(item, "source_type", csilk_json_get_string(r, "source_type"));
        csilk_json_add_string(item, "direction", csilk_json_get_string(r, "direction"));
        csilk_json_add_string(
            item, "linked_direction", csilk_json_get_string(r, "linked_direction"));

        int mask_bal =
            (db_get_int(r, "mask_bal_a") != 0 || db_get_int(r, "mask_bal_la") != 0) ? 1 : 0;
        if (mask_bal) {
            csilk_json_add_number(item, "amount", 0.0);
            csilk_json_add_number(item, "price_per_unit", 0.0);
            csilk_json_add_number(item, "quantity", 0.0);
            csilk_json_add_bool(item, "is_balance_masked", true);
        } else {
            csilk_json_add_number(item, "amount", db_get_num(r, "amount"));
            csilk_json_add_number(item, "price_per_unit", db_get_num(r, "price_per_unit"));
            csilk_json_add_number(item, "quantity", db_get_num(r, "quantity"));
            csilk_json_add_bool(item, "is_balance_masked", false);
        }
        csilk_json_add_string(item, "currency", csilk_json_get_string(r, "currency"));
        csilk_json_add_string(
            item, "transaction_date", csilk_json_get_string(r, "transaction_date"));
        csilk_json_add_string(item, "note", csilk_json_get_string(r, "note"));
        csilk_json_add_int(item, "user_id", db_get_int(r, "user_id"));
        const char* op_user = csilk_json_get_string(r, "operator_username");
        if (op_user && op_user[0]) {
            csilk_json_add_string(item, "operator_username", op_user);
        }
        csilk_json_add_string(item, "asset_name", csilk_json_get_string(r, "asset_name"));
        csilk_json_add_string(
            item, "linked_asset_name", csilk_json_get_string(r, "linked_asset_name"));
        csilk_json_add_string(item, "category_name", csilk_json_get_string(r, "category_name"));
        csilk_json_array_append(out, item);
    }
    csilk_json_free(raw_rows);
    return out;
}

csilk_json_t*
tx_list(csilk_db_pool_t* pool,
        int64_t          user_id,
        int64_t          ledger_id,
        int64_t          page,
        int64_t          page_size,
        const char*      asset_id,
        const char*      category_id,
        const char*      type,
        const char*      source_type,
        const char*      start_date,
        const char*      end_date,
        int64_t*         total)
{
    char uid[32], limit[32], offset[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(limit, sizeof(limit), "%lld", (long long)page_size);
    snprintf(offset, sizeof(offset), "%lld", (long long)((page - 1) * page_size));

    /* 动态条件片段与参数一一对应，避免 strncat 拼接导致参数数组越界或
       条件片段与参数错位（AGENTS.md: 动态 SQL 必须用匹配 %s 指定符）。 */
    char cond_sql[512] = {0};
    int  cond_len = 0;

    const char *params[32], *cnt_params[24];
    int         pidx = 0, cnt_pidx = 0;
    /* SELECT 中 2 个脱敏列共 4 个 uid 参数 */
    params[pidx++] = uid;
    params[pidx++] = uid;
    params[pidx++] = uid;
    params[pidx++] = uid;

    char where_scope[384];
    if (ledger_id > 0) {
        snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
        snprintf(where_scope,
                 sizeof(where_scope),
                 "t.ledger_id=? AND (t.user_id=? OR EXISTS (SELECT 1 FROM ledgers _l WHERE "
                 "_l.id=t.ledger_id AND _l.owner_id=?) OR EXISTS (SELECT 1 FROM ledger_members lm "
                 "WHERE lm.ledger_id=t.ledger_id AND lm.user_id=?))");
        params[pidx++] = lid;
        params[pidx++] = uid;
        params[pidx++] = uid;
        params[pidx++] = uid;
        cnt_params[cnt_pidx++] = lid;
        cnt_params[cnt_pidx++] = uid;
        cnt_params[cnt_pidx++] = uid;
        cnt_params[cnt_pidx++] = uid;
    } else {
        snprintf(where_scope, sizeof(where_scope), "t.user_id=?");
        params[pidx++] = uid;
        cnt_params[cnt_pidx++] = uid;
    }

    /* 可见性 (a + la) 共 4 个 uid 参数 */
    params[pidx++] = uid;
    params[pidx++] = uid;
    params[pidx++] = uid;
    params[pidx++] = uid;

    cnt_params[cnt_pidx++] = uid;
    cnt_params[cnt_pidx++] = uid;
    cnt_params[cnt_pidx++] = uid;
    cnt_params[cnt_pidx++] = uid;

    if (asset_id && asset_id[0]) {
        cond_len +=
            snprintf(cond_sql + cond_len, sizeof(cond_sql) - cond_len, " AND t.asset_id = ?");
        params[pidx++] = asset_id;
        cnt_params[cnt_pidx++] = asset_id;
    }
    if (category_id && category_id[0]) {
        cond_len +=
            snprintf(cond_sql + cond_len, sizeof(cond_sql) - cond_len, " AND t.category_id = ?");
        params[pidx++] = category_id;
        cnt_params[cnt_pidx++] = category_id;
    }
    if (type && type[0]) {
        cond_len += snprintf(
            cond_sql + cond_len, sizeof(cond_sql) - cond_len, " AND t.transaction_type = ?");
        params[pidx++] = type;
        cnt_params[cnt_pidx++] = type;
    }
    if (source_type && source_type[0]) {
        cond_len +=
            snprintf(cond_sql + cond_len, sizeof(cond_sql) - cond_len, " AND t.source_type = ?");
        params[pidx++] = source_type;
        cnt_params[cnt_pidx++] = source_type;
    }
    if (start_date && start_date[0]) {
        cond_len += snprintf(
            cond_sql + cond_len, sizeof(cond_sql) - cond_len, " AND t.transaction_date >= ?");
        params[pidx++] = start_date;
        cnt_params[cnt_pidx++] = start_date;
    }
    if (end_date && end_date[0]) {
        cond_len += snprintf(
            cond_sql + cond_len, sizeof(cond_sql) - cond_len, " AND t.transaction_date <= ?");
        params[pidx++] = end_date;
        cnt_params[cnt_pidx++] = end_date;
    }

    /* 守恒断言：条件片段长度不得溢出缓冲，参数个数不得超过数组容量 */
    if (cond_len < 0 || cond_len >= (int)sizeof(cond_sql)) {
        *total = 0;
        return NULL;
    }
    if (pidx + 3 > 32 || cnt_pidx + 1 > 24) { /* +2: LIMIT/OFFSET, +1: NULL */
        *total = 0;
        return NULL;
    }

    char sql[2560];
    char count_sql[1536];
    snprintf(
        sql,
        sizeof(sql),
        "SELECT "
        "t.id,t.user_id,t.asset_id,t.linked_asset_id,t.category_id,t.transaction_type,t.source_"
        "type,t."
        "direction,t.linked_direction,t.amount,t.price_per_unit,t.quantity,t.currency,t."
        "transaction_date,t.note,a.name as asset_name,la.name as linked_asset_name,c.name as "
        "category_name,u.username as operator_username," MF_RBAC_MASK_BALANCE_COL(
            "a") " AS mask_bal_a," MF_RBAC_MASK_BALANCE_COL("la") " AS mask_bal_la "
                                                                  "FROM transactions t LEFT JOIN "
                                                                  "assets a ON t.asset_id=a.id "
                                                                  "LEFT JOIN "
                                                                  "assets la ON "
                                                                  "t.linked_asset_id=la.id LEFT "
                                                                  "JOIN categories c ON "
                                                                  "t.category_id=c.id "
                                                                  "LEFT JOIN users u ON "
                                                                  "t.user_id=u.id "
                                                                  "WHERE %s "
                                                                  "AND " MF_RBAC_ASSET_VISIBLE_SQL("a") " AND " MF_RBAC_ASSET_VISIBLE_SQL(
                                                                      "la") "%s "
                                                                            "ORDER BY "
                                                                            "t.transaction_date "
                                                                            "DESC LIMIT ? OFFSET ?",
        where_scope,
        cond_sql);
    params[pidx++] = limit;
    params[pidx++] = offset;
    params[pidx] = NULL;

    snprintf(
        count_sql,
        sizeof(count_sql),
        "SELECT COUNT(*) AS cnt FROM transactions t "
        "LEFT JOIN assets a ON t.asset_id=a.id "
        "LEFT JOIN assets la ON t.linked_asset_id=la.id "
        "WHERE %s AND " MF_RBAC_ASSET_VISIBLE_SQL("a") " AND " MF_RBAC_ASSET_VISIBLE_SQL("la") "%s",
        where_scope,
        cond_sql);
    cnt_params[cnt_pidx] = NULL;

    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    *total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }
    csilk_json_t* raw_rows = csilk_db_query_param_json(pool, sql, params);
    return raw_rows ? format_tx_rows(raw_rows) : NULL;
}

csilk_json_t*
tx_monthly(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id, const char* pattern)
{
    char uid[32], lid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    if (ledger_id > 0) {
        snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
        return csilk_db_query_param_json(
            pool,
            "SELECT COALESCE(SUM(t.amount),0) AS total_volume, "
            "COALESCE(SUM(CASE WHEN t.direction='in' THEN t.amount ELSE 0 END),0) AS inflows, "
            "COALESCE(SUM(CASE WHEN t.direction='out' THEN t.amount ELSE 0 END),0) AS outflows, "
            "COUNT(*) AS count FROM transactions t "
            "LEFT JOIN assets a ON t.asset_id=a.id "
            "WHERE t.ledger_id=? AND (t.user_id=? OR EXISTS (SELECT 1 FROM ledgers _l WHERE "
            "_l.id=t.ledger_id AND _l.owner_id=?) OR EXISTS (SELECT 1 FROM ledger_members lm WHERE "
            "lm.ledger_id=t.ledger_id AND lm.user_id=?)) "
            "AND CAST(t.transaction_date AS TEXT) LIKE ? "
            "AND " MF_RBAC_ASSET_UNMASKED_SQL("a"),
            (const char*[]){lid, uid, uid, uid, pattern, uid, uid, NULL});
    }
    return csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(SUM(t.amount),0) AS total_volume, "
        "COALESCE(SUM(CASE WHEN t.direction='in' THEN t.amount ELSE 0 END),0) AS inflows, "
        "COALESCE(SUM(CASE WHEN t.direction='out' THEN t.amount ELSE 0 END),0) AS outflows, "
        "COUNT(*) AS count FROM transactions t "
        "LEFT JOIN assets a ON t.asset_id=a.id "
        "WHERE t.user_id=? "
        "AND CAST(t.transaction_date AS TEXT) LIKE ? "
        "AND " MF_RBAC_ASSET_UNMASKED_SQL("a"),
        (const char*[]){uid, pattern, uid, uid, NULL});
}

int64_t
tx_insert(csilk_db_pool_t* pool,
          int64_t          user_id,
          int64_t          asset_id,
          int64_t          linked_asset_id,
          int64_t          category_id,
          const char*      source_type,
          const char*      transaction_type,
          const char*      direction,
          const char*      linked_direction,
          double           amount,
          double           price_per_unit,
          double           quantity,
          double           fee,
          const char*      currency,
          const char*      date,
          const char*      note)
{
    char uid[32], ast[32], last[32], cat[32], amt[64], pp[64], qty[64], fee_s[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    snprintf(last, sizeof(last), "%lld", (long long)linked_asset_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    snprintf(pp, sizeof(pp), "%.4f", price_per_unit);
    snprintf(qty, sizeof(qty), "%.4f", quantity);
    snprintf(fee_s, sizeof(fee_s), "%.6f", fee);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions "
        "(user_id,asset_id,linked_asset_id,category_id,source_type,transaction_type,direction,"
        "linked_direction,amount,price_per_unit,quantity,fee,currency,transaction_date,note,ledger_"
        "id) "
        "VALUES (?,?,NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0),NULLIF(CAST(NULLIF(?, '') AS "
        "BIGINT), 0),?,?,?, "
        "NULLIF(?, ''),?,?,?,?,?,?,?, "
        "COALESCE((SELECT ledger_id FROM assets WHERE id = ?), (SELECT id FROM ledgers WHERE "
        "owner_id = ? AND is_default = 1))) RETURNING id",
        (const char*[]){uid,
                        ast,
                        last,
                        cat,
                        source_type,
                        transaction_type,
                        (direction && direction[0]) ? direction : "out",
                        (linked_direction && linked_direction[0]) ? linked_direction : "",
                        amt,
                        pp,
                        qty,
                        fee_s,
                        currency ? currency : "CNY",
                        date,
                        note ? note : "",
                        ast,
                        uid,
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
tx_update_fx_rate(void* pool, int64_t user_id, int64_t tx_id, double fx_rate)
{
    if (!pool || user_id <= 0 || tx_id <= 0) {
        return -1;
    }
    char uid_str[32], tid_str[32], rate_str[64];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(tid_str, sizeof(tid_str), "%lld", (long long)tx_id);
    snprintf(rate_str, sizeof(rate_str), "%.8f", fx_rate);
    csilk_json_t* fx_res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "UPDATE transactions SET fx_rate_to_cny = ? WHERE id = ? AND user_id = ? RETURNING id",
        (const char*[]){rate_str, tid_str, uid_str, NULL});
    if (!fx_res || csilk_json_array_size(fx_res) == 0) {
        if (fx_res) {
            csilk_json_free(fx_res);
        }
        return -1;
    }
    csilk_json_free(fx_res);
    return 0;
}

csilk_json_t*
tx_get_raw(void* pool, int64_t user_id, int64_t id)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return NULL;
    }
    char uid_str[32], tid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(tid_str, sizeof(tid_str), "%lld", (long long)id);
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT id, transaction_type, parent_tx_id, asset_id, linked_asset_id, amount, "
        "transaction_date FROM transactions WHERE id = ? AND user_id = ?",
        (const char*[]){tid_str, uid_str, NULL});
}

int
tx_delete_transfer_in_by_parent(void*   pool,
                                int64_t user_id,
                                int64_t parent_tx_id,
                                int64_t in_asset_id,
                                int64_t out_asset_id,
                                double  amount)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    char uid_str[32], tid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(tid_str, sizeof(tid_str), "%lld", (long long)parent_tx_id);
    csilk_json_t* in_del = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "DELETE FROM transactions WHERE user_id=? AND parent_tx_id=? RETURNING id",
        (const char*[]){uid_str, tid_str, NULL});
    if (!in_del || csilk_json_array_size(in_del) == 0) {
        char laid_str[32], aid_str[32], amt_str[64];
        snprintf(laid_str, sizeof(laid_str), "%lld", (long long)in_asset_id);
        snprintf(aid_str, sizeof(aid_str), "%lld", (long long)out_asset_id);
        snprintf(amt_str, sizeof(amt_str), "%.6f", amount);
        csilk_json_free(csilk_db_query_param_json(
            (csilk_db_pool_t*)pool,
            "DELETE FROM transactions WHERE id IN ("
            "  SELECT id FROM transactions WHERE user_id=? AND transaction_type='transfer_in' "
            "  AND asset_id=? AND linked_asset_id=? AND amount=? ORDER BY id DESC LIMIT 1)",
            (const char*[]){uid_str, laid_str, aid_str, amt_str, NULL}));
    }
    if (in_del) {
        csilk_json_free(in_del);
    }
    return 0;
}

int64_t
tx_find_transfer_out_by_match(
    void* pool, int64_t user_id, int64_t in_asset_id, int64_t out_asset_id, double amount)
{
    if (!pool || user_id <= 0) {
        return 0;
    }
    char uid_str[32], laid_str[32], aid_str[32], amt_str[64];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(laid_str, sizeof(laid_str), "%lld", (long long)in_asset_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)out_asset_id);
    snprintf(amt_str, sizeof(amt_str), "%.6f", amount);

    csilk_json_t* f_rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT id FROM transactions WHERE user_id=? AND transaction_type='transfer_out' "
        "AND asset_id=? AND linked_asset_id=? AND amount=? ORDER BY id DESC LIMIT 1",
        (const char*[]){uid_str, laid_str, aid_str, amt_str, NULL});
    int64_t found_id = 0;
    if (f_rows && csilk_json_array_size(f_rows) > 0) {
        found_id = (int64_t)db_get_int(csilk_json_array_get(f_rows, 0), "id");
    }
    if (f_rows) {
        csilk_json_free(f_rows);
    }
    return found_id;
}

int
tx_delete_by_id_or_parent(void* pool, int64_t user_id, int64_t id)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return -1;
    }
    char uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "DELETE FROM transactions WHERE user_id=? AND (id=? OR parent_tx_id=?)",
        (const char*[]){uid_str, id_str, id_str, NULL});
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}
