#include "infrastructure/repositories/transaction_repo_impl.h"
#include "infrastructure/repositories/transaction_repo_impl.h"
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
        "FROM transactions WHERE id=? AND user_id=?",
        (const char*[]){id_str, uid_str, NULL});

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

    quantity_to_string_fixed(tx->amount, 8, amt_str, sizeof(amt_str));
    price_to_string_fixed(tx->price, 8, price_str, sizeof(price_str));
    quantity_to_string_fixed(tx->amount, 8, qty_str, sizeof(qty_str));
    money_to_string(tx->fee, fee_str, sizeof(fee_str));

    const char* currency = tx->fee_currency[0] ? tx->fee_currency : "CNY";
    const char* date = tx->tx_time[0] ? tx->tx_time : "2026-01-01";
    const char* note = tx->note;

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "INSERT INTO transactions (user_id, asset_id, linked_asset_id, parent_tx_id, "
        "transaction_type, "
        "amount, price_per_unit, quantity, fee, currency, note, transaction_date) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
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

    quantity_to_string_fixed(tx->amount, 8, amt_str, sizeof(amt_str));
    price_to_string_fixed(tx->price, 8, price_str, sizeof(price_str));
    quantity_to_string_fixed(tx->amount, 8, qty_str, sizeof(qty_str));
    money_to_string(tx->fee, fee_str, sizeof(fee_str));

    const char* currency = tx->fee_currency[0] ? tx->fee_currency : "CNY";
    const char* date = tx->tx_time[0] ? tx->tx_time : "2026-01-01";
    const char* note = tx->note;

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "UPDATE transactions SET asset_id=?, linked_asset_id=?, transaction_type=?, "
        "amount=?, price_per_unit=?, quantity=?, fee=?, currency=?, note=?, transaction_date=? "
        "WHERE id=? AND user_id=? RETURNING id",
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
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "DELETE FROM transactions WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){idstr, uid, NULL});
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
        pool, "SELECT id FROM assets WHERE id=? AND user_id=?", (const char*[]){ast, uid, NULL});
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

/* --- Inlined: tx_list, tx_insert, tx_monthly --- */

csilk_json_t*
tx_list(csilk_db_pool_t* pool,
        int64_t          user_id,
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
    char uid[32], limit[32], offset[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(limit, sizeof(limit), "%lld", (long long)page_size);
    snprintf(offset, sizeof(offset), "%lld", (long long)((page - 1) * page_size));
    char        sql[1024], count_sql[1024];
    const char *params[16], *cnt_params[16];
    int         pidx = 0, cnt_pidx = 0;
    params[pidx++] = uid;
    cnt_params[cnt_pidx++] = uid;
    snprintf(sql,
             sizeof(sql),
             "SELECT "
             "t.id,t.asset_id,t.linked_asset_id,t.category_id,t.transaction_type,t.source_type,t."
             "direction,t.linked_direction,t.amount,t.price_per_unit,t.quantity,t.currency,t."
             "transaction_date,t.note,a.name as asset_name,la.name as linked_asset_name,c.name as "
             "category_name FROM transactions t LEFT JOIN assets a ON t.asset_id=a.id LEFT JOIN "
             "assets la ON t.linked_asset_id=la.id LEFT JOIN categories c ON t.category_id=c.id "
             "WHERE t.user_id=?");
    snprintf(count_sql,
             sizeof(count_sql),
             "SELECT COUNT(*) AS cnt FROM transactions t WHERE t.user_id=?");
#define AF(col, val)                                                                               \
    do {                                                                                           \
        strncat(sql, " AND t." col "=?", sizeof(sql) - strlen(sql) - 1);                           \
        strncat(count_sql, " AND t." col "=?", sizeof(count_sql) - strlen(count_sql) - 1);         \
        params[pidx++] = (val);                                                                    \
        cnt_params[cnt_pidx++] = (val);                                                            \
    } while (0)
    if (asset_id) {
        AF("asset_id", asset_id);
    }
    if (category_id) {
        AF("category_id", category_id);
    }
    if (type) {
        AF("transaction_type", type);
    }
    if (source_type) {
        AF("source_type", source_type);
    }
    if (start_date) {
        strncat(sql, " AND t.transaction_date >= ?", sizeof(sql) - strlen(sql) - 1);
        strncat(
            count_sql, " AND t.transaction_date >= ?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = start_date;
        cnt_params[cnt_pidx++] = start_date;
    }
    if (end_date) {
        strncat(sql, " AND t.transaction_date <= ?", sizeof(sql) - strlen(sql) - 1);
        strncat(
            count_sql, " AND t.transaction_date <= ?", sizeof(count_sql) - strlen(count_sql) - 1);
        params[pidx++] = end_date;
        cnt_params[cnt_pidx++] = end_date;
    }
#undef AF
    strncat(
        sql, " ORDER BY t.transaction_date DESC LIMIT ? OFFSET ?", sizeof(sql) - strlen(sql) - 1);
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
tx_monthly(csilk_db_pool_t* pool, int64_t user_id, const char* pattern)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(SUM(amount),0) AS total_volume,COALESCE(SUM(CASE WHEN direction='in' THEN "
        "amount ELSE 0 END),0) AS inflows,COALESCE(SUM(CASE WHEN direction='out' THEN amount ELSE "
        "0 END),0) AS outflows,COUNT(*) AS count FROM transactions WHERE user_id=? AND "
        "transaction_date LIKE ?",
        (const char*[]){uid, pattern, NULL});
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
        "linked_direction,amount,price_per_unit,quantity,fee,currency,transaction_date,note) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) RETURNING id",
        (const char*[]){uid,
                        ast,
                        linked_asset_id > 0 ? last : "NULL",
                        cat,
                        source_type,
                        transaction_type,
                        direction ? "in" : "out",
                        linked_direction ? "out" : NULL,
                        amt,
                        pp,
                        qty,
                        fee_s,
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
