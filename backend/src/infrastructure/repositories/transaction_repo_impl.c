#include "infrastructure/repositories/transaction_repo_impl.h"
#include "repositories/transaction_repo.h"
#include "common/db.h"
#include "core/financial/currency.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mf_tx_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_transaction_t* out_tx) {
    if (!db_pool || user_id <= 0 || id <= 0 || !out_tx) return -1;
    memset(out_tx, 0, sizeof(*out_tx));

    char uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT id, user_id, asset_id, linked_asset_id, parent_tx_id, transaction_type, "
        "amount, price_per_unit, quantity, fee, currency, note, transaction_date, created_at, updated_at "
        "FROM transactions WHERE id=? AND user_id=?",
        (const char*[]){id_str, uid_str, NULL}
    );

    if (!res || csilk_json_array_size(res) == 0) {
        if (res) csilk_json_free(res);
        return 1; /* Not found */
    }

    csilk_json_t* row = csilk_json_array_get(res, 0);
    out_tx->id = db_get_int(row, "id");
    out_tx->user_id = db_get_int(row, "user_id");
    out_tx->asset_id = db_get_int(row, "asset_id");
    out_tx->account_id = db_get_int(row, "linked_asset_id");
    out_tx->parent_tx_id = db_get_int(row, "parent_tx_id");

    const char* type = csilk_json_get_string(row, "transaction_type");
    if (type) snprintf(out_tx->type, sizeof(out_tx->type), "%s", type);

    const char* cur_code = csilk_json_get_string(row, "currency");
    if (!cur_code) cur_code = "CNY";
    snprintf(out_tx->fee_currency, sizeof(out_tx->fee_currency), "%s", cur_code);
    currency_t cur = currency_from_str(cur_code);

    double amount_val = db_get_num(row, "amount");
    double price_val = db_get_num(row, "price_per_unit");
    double qty_val = db_get_num(row, "quantity");
    double fee_val = db_get_num(row, "fee");

    quantity_from_double((qty_val > 0) ? qty_val : amount_val, 4, &out_tx->amount);
    price_from_double(price_val, 4, cur, &out_tx->price);
    money_from_double(fee_val, cur, &out_tx->fee);

    const char* note = csilk_json_get_string(row, "note");
    if (note) snprintf(out_tx->note, sizeof(out_tx->note), "%s", note);

    const char* tx_date = csilk_json_get_string(row, "transaction_date");
    if (tx_date) snprintf(out_tx->tx_time, sizeof(out_tx->tx_time), "%s", tx_date);

    const char* cat = csilk_json_get_string(row, "created_at");
    if (cat) snprintf(out_tx->created_at, sizeof(out_tx->created_at), "%s", cat);

    const char* uat = csilk_json_get_string(row, "updated_at");
    if (uat) snprintf(out_tx->updated_at, sizeof(out_tx->updated_at), "%s", uat);

    csilk_json_free(res);
    return 0;
}

int mf_tx_repo_save(void* db_pool, const mf_transaction_t* tx, int64_t* out_id) {
    if (!db_pool || !tx) return -1;

    char uid_str[32], asset_id_str[32], linked_asset_id_str[32], parent_id_str[32];
    char amt_str[64], price_str[64], qty_str[64], fee_str[64];

    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)tx->user_id);
    snprintf(asset_id_str, sizeof(asset_id_str), "%lld", (long long)tx->asset_id);
    snprintf(linked_asset_id_str, sizeof(linked_asset_id_str), "%lld", (long long)tx->account_id);
    snprintf(parent_id_str, sizeof(parent_id_str), "%lld", (long long)tx->parent_tx_id);

    double amount_val = quantity_to_double(tx->amount);
    double price_val = price_to_double(tx->price);
    double fee_val = money_to_double(tx->fee);

    snprintf(amt_str, sizeof(amt_str), "%.8f", amount_val);
    snprintf(price_str, sizeof(price_str), "%.8f", price_val);
    snprintf(qty_str, sizeof(qty_str), "%.8f", amount_val);
    snprintf(fee_str, sizeof(fee_str), "%.8f", fee_val);

    const char* currency = tx->fee_currency[0] ? tx->fee_currency : "CNY";
    const char* date = tx->tx_time[0] ? tx->tx_time : "2026-01-01";
    const char* note = tx->note;

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "INSERT INTO transactions (user_id, asset_id, linked_asset_id, parent_tx_id, transaction_type, "
        "amount, price_per_unit, quantity, fee, currency, note, transaction_date) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
        (const char*[]){uid_str, asset_id_str, linked_asset_id_str, parent_id_str, tx->type,
                        amt_str, price_str, qty_str, fee_str, currency, note, date, NULL}
    );

    if (!res || csilk_json_array_size(res) == 0) {
        if (res) csilk_json_free(res);
        return -1;
    }

    if (out_id) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        *out_id = db_get_int(row, "id");
    }

    csilk_json_free(res);
    return 0;
}

int mf_tx_repo_update(void* db_pool, const mf_transaction_t* tx) {
    if (!db_pool || !tx || tx->id <= 0) return -1;

    char id_str[32], uid_str[32], asset_id_str[32], linked_asset_id_str[32];
    char amt_str[64], price_str[64], qty_str[64], fee_str[64];

    snprintf(id_str, sizeof(id_str), "%lld", (long long)tx->id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)tx->user_id);
    snprintf(asset_id_str, sizeof(asset_id_str), "%lld", (long long)tx->asset_id);
    snprintf(linked_asset_id_str, sizeof(linked_asset_id_str), "%lld", (long long)tx->account_id);

    double amount_val = quantity_to_double(tx->amount);
    double price_val = price_to_double(tx->price);
    double fee_val = money_to_double(tx->fee);

    snprintf(amt_str, sizeof(amt_str), "%.8f", amount_val);
    snprintf(price_str, sizeof(price_str), "%.8f", price_val);
    snprintf(qty_str, sizeof(qty_str), "%.8f", amount_val);
    snprintf(fee_str, sizeof(fee_str), "%.8f", fee_val);

    const char* currency = tx->fee_currency[0] ? tx->fee_currency : "CNY";
    const char* date = tx->tx_time[0] ? tx->tx_time : "2026-01-01";
    const char* note = tx->note;

    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "UPDATE transactions SET asset_id=?, linked_asset_id=?, transaction_type=?, "
        "amount=?, price_per_unit=?, quantity=?, fee=?, currency=?, note=?, transaction_date=? "
        "WHERE id=? AND user_id=? RETURNING id",
        (const char*[]){asset_id_str, linked_asset_id_str, tx->type, amt_str, price_str, qty_str, fee_str,
                        currency, note, date, id_str, uid_str, NULL}
    );

    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : -1;
    if (res) csilk_json_free(res);
    return ok;
}

int mf_tx_repo_delete(void* db_pool, int64_t user_id, int64_t id) {
    return tx_delete((csilk_db_pool_t*)db_pool, user_id, id) ? 0 : -1;
}

int mf_tx_repo_find_fee_children(void* db_pool, int64_t user_id, int64_t parent_tx_id,
                                 mf_transaction_t** out_list, size_t* out_count) {
    if (!db_pool || user_id <= 0 || parent_tx_id <= 0 || !out_list || !out_count) return -1;
    *out_list = NULL;
    *out_count = 0;

    csilk_json_t* rows = tx_child_fee_rows((csilk_db_pool_t*)db_pool, user_id, parent_tx_id);
    if (!rows) return 0;

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

        double amt = db_get_num(r, "amount");
        quantity_from_double(amt, 4, &list[i].amount);
        money_from_double(amt, cny, &list[i].fee);
        price_from_money(list[i].fee);

        const char* note = csilk_json_get_string(r, "note");
        if (note) snprintf(list[i].note, sizeof(list[i].note), "%s", note);
    }

    csilk_json_free(rows);
    *out_list = list;
    *out_count = count;
    return 0;
}

int mf_tx_repo_delete_fee_children(void* db_pool, int64_t user_id, int64_t parent_tx_id) {
    return tx_delete_fee_children((csilk_db_pool_t*)db_pool, user_id, parent_tx_id) ? 0 : -1;
}

void mf_tx_repo_free_list(mf_transaction_t* list, size_t count) {
    (void)count;
    if (list) free(list);
}
