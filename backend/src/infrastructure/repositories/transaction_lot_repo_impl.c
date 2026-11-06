#include "infrastructure/repositories/transaction_lot_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t
tx_lot_create(csilk_db_pool_t* pool,
              int64_t          user_id,
              int64_t          asset_id,
              int64_t          buy_tx_id,
              const char*      buy_date,
              quantity_t       qty,
              price_t          cost_per_unit,
              money_t          fee)
{
    if (!pool || user_id <= 0 || asset_id <= 0 || buy_tx_id <= 0 || !buy_date || !buy_date[0]) {
        return -1;
    }

    char uid_str[32], ast_str[32], tx_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);
    snprintf(tx_str, sizeof(tx_str), "%lld", (long long)buy_tx_id);

    char qty_str[64] = "0.00000000";
    quantity_to_string_fixed(qty, 8, qty_str, sizeof(qty_str));

    char cpu_str[64] = "0.00";
    price_to_string(cost_per_unit, cpu_str, sizeof(cpu_str));

    char fee_str[64] = "0.00";
    money_to_string(fee, fee_str, sizeof(fee_str));

    const char* sql =
        "INSERT INTO transaction_lots "
        "(user_id, asset_id, buy_tx_id, buy_date, original_quantity, remaining_quantity, "
        "cost_per_unit, fee_allocated, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'open') RETURNING id";

    const char* params[] = {
        uid_str, ast_str, tx_str, buy_date, qty_str, qty_str, cpu_str, fee_str, NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int64_t       id = -1;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
        if (id <= 0) {
            id = -1;
        }
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

csilk_json_t*
tx_lots_get_open(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return NULL;
    }

    char uid_str[32], ast_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);

    const char* sql = "SELECT id, user_id, asset_id, buy_tx_id, buy_date, original_quantity, "
                      "remaining_quantity, cost_per_unit, fee_allocated, status "
                      "FROM transaction_lots "
                      "WHERE user_id=? AND asset_id=? AND status='open' "
                      "ORDER BY buy_date ASC, id ASC";

    const char* params[] = {uid_str, ast_str, NULL};
    return csilk_db_query_param_json(pool, sql, params);
}

int
tx_lot_deduct(csilk_db_pool_t* pool,
              int64_t          user_id,
              int64_t          lot_id,
              quantity_t       deduct_qty,
              quantity_t       new_remaining_qty,
              bool             is_closed)
{
    (void)deduct_qty;
    if (!pool || user_id <= 0 || lot_id <= 0) {
        return -1;
    }

    char rem_qty_str[64] = "0.00000000";
    quantity_to_string_fixed(new_remaining_qty, 8, rem_qty_str, sizeof(rem_qty_str));

    const char* status_str = is_closed ? "closed" : "open";
    char        lot_id_str[32];
    snprintf(lot_id_str, sizeof(lot_id_str), "%lld", (long long)lot_id);
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char* sql = "UPDATE transaction_lots SET remaining_quantity=?, status=? WHERE id=? AND "
                      "user_id=? RETURNING id";
    const char* params[] = {rem_qty_str, status_str, lot_id_str, uid_str, NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    if (!res) {
        return -1;
    }
    int ok = (csilk_json_array_size(res) > 0);
    csilk_json_free(res);
    return ok ? 0 : -1;
}

int
tx_lots_delete_by_asset(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return -1;
    }

    char uid_str[32], ast_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);

    const char* sql = "DELETE FROM transaction_lots WHERE user_id=? AND asset_id=?";
    const char* params[] = {uid_str, ast_str, NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    if (!res) {
        return -1;
    }
    csilk_json_free(res);
    return 0;
}

int
tx_lots_delete_by_buy_tx(csilk_db_pool_t* pool, int64_t user_id, int64_t buy_tx_id)
{
    if (!pool || user_id <= 0 || buy_tx_id <= 0) {
        return -1;
    }

    char uid_str[32], tx_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(tx_str, sizeof(tx_str), "%lld", (long long)buy_tx_id);

    const char* sql = "DELETE FROM transaction_lots WHERE user_id=? AND buy_tx_id=?";
    const char* params[] = {uid_str, tx_str, NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    if (!res) {
        return -1;
    }
    csilk_json_free(res);
    return 0;
}

char*
tx_lots_serialize_open_json(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return strdup("[]");
    }

    csilk_json_t* lots = tx_lots_get_open(pool, user_id, asset_id);
    if (!lots || csilk_json_array_size(lots) == 0) {
        if (lots) {
            csilk_json_free(lots);
        }
        return strdup("[]");
    }

    char* json_str = csilk_json_serialize(lots, NULL);
    csilk_json_free(lots);
    if (!json_str) {
        return strdup("[]");
    }
    return json_str;
}
