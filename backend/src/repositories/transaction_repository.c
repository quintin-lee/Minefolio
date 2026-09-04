#include "repositories/transaction_repository.h"
#include "infrastructure/database/statement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
tx_repo_find_by_id(mf_db_t* db, int64_t user_id, int64_t id, tx_record_t* out_tx)
{
    if (!db || !out_tx) {
        return -1;
    }
    memset(out_tx, 0, sizeof(*out_tx));

    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(db,
                                    "SELECT id, user_id, asset_id, "
                                    "COALESCE(linked_asset_id, 0) AS linked_asset_id, "
                                    "COALESCE(category_id, 0) AS category_id, "
                                    "COALESCE(parent_tx_id, 0) AS parent_tx_id, "
                                    "transaction_type, amount, fee, price, quantity, "
                                    "COALESCE(direction, 'out') AS direction, "
                                    "COALESCE(linked_direction, '') AS linked_direction, "
                                    "transaction_time, COALESCE(note, '') AS note, created_at "
                                    "FROM transactions "
                                    "WHERE user_id = ? AND id = ?;",
                                    &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    mf_stmt_bind_int64(stmt, 1, user_id);
    mf_stmt_bind_int64(stmt, 2, id);

    mf_result_t* res = NULL;
    rc = mf_stmt_query(stmt, &res);
    if (rc != 0 || !res) {
        mf_stmt_close(stmt);
        return -1;
    }

    if (!mf_result_next(res)) {
        mf_result_free(res);
        mf_stmt_close(stmt);
        return 1; /* 不存在 */
    }

    out_tx->id = mf_result_get_int64(res, "id");
    out_tx->user_id = mf_result_get_int64(res, "user_id");
    out_tx->asset_id = mf_result_get_int64(res, "asset_id");
    out_tx->linked_asset_id = mf_result_get_int64(res, "linked_asset_id");
    out_tx->category_id = mf_result_get_int64(res, "category_id");
    out_tx->parent_tx_id = mf_result_get_int64(res, "parent_tx_id");

    snprintf(out_tx->transaction_type,
             sizeof(out_tx->transaction_type),
             "%s",
             mf_result_get_text(res, "transaction_type"));
    out_tx->amount = mf_result_get_double(res, "amount");
    out_tx->fee = mf_result_get_double(res, "fee");
    out_tx->price = mf_result_get_double(res, "price");
    out_tx->quantity = mf_result_get_double(res, "quantity");
    snprintf(
        out_tx->direction, sizeof(out_tx->direction), "%s", mf_result_get_text(res, "direction"));
    snprintf(out_tx->linked_direction,
             sizeof(out_tx->linked_direction),
             "%s",
             mf_result_get_text(res, "linked_direction"));
    snprintf(out_tx->transaction_time,
             sizeof(out_tx->transaction_time),
             "%s",
             mf_result_get_text(res, "transaction_time"));
    snprintf(out_tx->note, sizeof(out_tx->note), "%s", mf_result_get_text(res, "note"));
    snprintf(out_tx->created_at,
             sizeof(out_tx->created_at),
             "%s",
             mf_result_get_text(res, "created_at"));

    mf_result_free(res);
    mf_stmt_close(stmt);
    return 0;
}

int64_t
tx_repo_insert(mf_db_t*    db,
               int64_t     user_id,
               int64_t     asset_id,
               int64_t     linked_asset_id,
               int64_t     category_id,
               const char* transaction_type,
               double      amount,
               double      fee,
               double      price,
               double      quantity,
               const char* direction,
               const char* linked_direction,
               const char* transaction_time,
               const char* note,
               int64_t     parent_tx_id)
{
    if (!db || !transaction_type) {
        return -1;
    }

    mf_stmt_t* stmt = NULL;
    int rc = mf_stmt_prepare(db,
                             "INSERT INTO transactions ("
                             "user_id, asset_id, linked_asset_id, category_id, transaction_type, "
                             "amount, fee, price, quantity, direction, linked_direction, "
                             "transaction_time, note, parent_tx_id"
                             ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id;",
                             &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    mf_stmt_bind_int64(stmt, 1, user_id);
    mf_stmt_bind_int64(stmt, 2, asset_id);
    if (linked_asset_id > 0) {
        mf_stmt_bind_int64(stmt, 3, linked_asset_id);
    } else {
        mf_stmt_bind_null(stmt, 3);
    }
    if (category_id > 0) {
        mf_stmt_bind_int64(stmt, 4, category_id);
    } else {
        mf_stmt_bind_null(stmt, 4);
    }
    mf_stmt_bind_text(stmt, 5, transaction_type);
    mf_stmt_bind_double(stmt, 6, amount);
    mf_stmt_bind_double(stmt, 7, fee);
    mf_stmt_bind_double(stmt, 8, price);
    mf_stmt_bind_double(stmt, 9, quantity);
    mf_stmt_bind_text(stmt, 10, direction ? direction : "out");
    if (linked_direction && linked_direction[0]) {
        mf_stmt_bind_text(stmt, 11, linked_direction);
    } else {
        mf_stmt_bind_null(stmt, 11);
    }
    mf_stmt_bind_text(stmt, 12, transaction_time ? transaction_time : "");
    mf_stmt_bind_text(stmt, 13, note ? note : "");
    if (parent_tx_id > 0) {
        mf_stmt_bind_int64(stmt, 14, parent_tx_id);
    } else {
        mf_stmt_bind_null(stmt, 14);
    }

    mf_result_t* res = NULL;
    rc = mf_stmt_query(stmt, &res);
    int64_t id = 0;
    if (rc == 0 && res && mf_result_next(res)) {
        id = mf_result_get_int64(res, "id");
    }
    if (res) {
        mf_result_free(res);
    }
    mf_stmt_close(stmt);
    return id;
}

int
tx_repo_delete(mf_db_t* db, int64_t user_id, int64_t id)
{
    if (!db) {
        return -1;
    }
    mf_stmt_t* stmt = NULL;
    int rc = mf_stmt_prepare(db, "DELETE FROM transactions WHERE id = ? AND user_id = ?;", &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }
    mf_stmt_bind_int64(stmt, 1, id);
    mf_stmt_bind_int64(stmt, 2, user_id);
    int64_t aff = 0;
    rc = mf_stmt_execute(stmt, &aff);
    mf_stmt_close(stmt);
    return (rc == 0 && aff > 0) ? 0 : -1;
}

int
tx_repo_delete_children_by_parent(mf_db_t* db, int64_t user_id, int64_t parent_tx_id)
{
    if (!db) {
        return -1;
    }
    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(
        db, "DELETE FROM transactions WHERE parent_tx_id = ? AND user_id = ?;", &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }
    mf_stmt_bind_int64(stmt, 1, parent_tx_id);
    mf_stmt_bind_int64(stmt, 2, user_id);
    int64_t aff = 0;
    rc = mf_stmt_execute(stmt, &aff);
    mf_stmt_close(stmt);
    return (rc == 0) ? 0 : -1;
}
