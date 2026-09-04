#include "repositories/asset_repository.h"
#include "infrastructure/database/statement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
asset_repo_find_by_id(mf_db_t* db, int64_t user_id, int64_t id, asset_record_t* out_asset)
{
    if (!db || !out_asset) {
        return -1;
    }
    memset(out_asset, 0, sizeof(*out_asset));

    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(db,
                             "SELECT a.id, a.user_id, a.category_id, a.name, a.account_no, "
                             "COALESCE(a.symbol, '') AS symbol, "
                             "COALESCE(a.quote_source, '') AS quote_source, "
                             "a.currency, a.note, c.name AS category_name, c.asset_type, "
                             "a.current_value, a.quantity, a.cost_basis, a.net_value, "
                             "COALESCE(CAST(a.last_sync_at AS TEXT), '') AS last_sync_at, "
                             "a.created_at, a.updated_at "
                             "FROM assets a "
                             "LEFT JOIN categories c ON a.category_id = c.id "
                             "WHERE a.user_id = ? AND a.id = ?;",
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

    out_asset->id = mf_result_get_int64(res, "id");
    out_asset->user_id = mf_result_get_int64(res, "user_id");
    out_asset->category_id = mf_result_get_int64(res, "category_id");
    snprintf(out_asset->name, sizeof(out_asset->name), "%s", mf_result_get_text(res, "name"));
    snprintf(out_asset->account_no, sizeof(out_asset->account_no), "%s",
             mf_result_get_text(res, "account_no"));
    snprintf(out_asset->symbol, sizeof(out_asset->symbol), "%s",
             mf_result_get_text(res, "symbol"));
    snprintf(out_asset->quote_source, sizeof(out_asset->quote_source), "%s",
             mf_result_get_text(res, "quote_source"));
    snprintf(out_asset->currency, sizeof(out_asset->currency), "%s",
             mf_result_get_text(res, "currency"));
    snprintf(out_asset->note, sizeof(out_asset->note), "%s", mf_result_get_text(res, "note"));
    snprintf(out_asset->category_name, sizeof(out_asset->category_name), "%s",
             mf_result_get_text(res, "category_name"));
    snprintf(out_asset->asset_type, sizeof(out_asset->asset_type), "%s",
             mf_result_get_text(res, "asset_type"));

    out_asset->current_value = mf_result_get_double(res, "current_value");
    out_asset->quantity = mf_result_get_double(res, "quantity");
    out_asset->cost_basis = mf_result_get_double(res, "cost_basis");
    out_asset->net_value = mf_result_get_double(res, "net_value");

    snprintf(out_asset->last_sync_at, sizeof(out_asset->last_sync_at), "%s",
             mf_result_get_text(res, "last_sync_at"));
    snprintf(out_asset->created_at, sizeof(out_asset->created_at), "%s",
             mf_result_get_text(res, "created_at"));
    snprintf(out_asset->updated_at, sizeof(out_asset->updated_at), "%s",
             mf_result_get_text(res, "updated_at"));

    mf_result_free(res);
    mf_stmt_close(stmt);
    return 0;
}

int64_t
asset_repo_insert(mf_db_t*    db,
                  int64_t     user_id,
                  int64_t     category_id,
                  const char* name,
                  const char* account_no,
                  double      current_value,
                  const char* currency,
                  const char* note,
                  double      quantity,
                  double      cost_basis,
                  double      net_value,
                  const char* symbol,
                  const char* quote_source)
{
    if (!db || !name) {
        return -1;
    }

    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(db,
                             "INSERT INTO assets ("
                             "user_id, category_id, name, account_no, current_value, currency, "
                             "note, quantity, cost_basis, net_value, symbol, quote_source"
                             ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id;",
                             &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    mf_stmt_bind_int64(stmt, 1, user_id);
    mf_stmt_bind_int64(stmt, 2, category_id);
    mf_stmt_bind_text(stmt, 3, name);
    mf_stmt_bind_text(stmt, 4, account_no ? account_no : "");
    mf_stmt_bind_double(stmt, 5, current_value);
    mf_stmt_bind_text(stmt, 6, currency && currency[0] ? currency : "CNY");
    mf_stmt_bind_text(stmt, 7, note ? note : "");
    mf_stmt_bind_double(stmt, 8, quantity);
    mf_stmt_bind_double(stmt, 9, cost_basis);
    mf_stmt_bind_double(stmt, 10, net_value);
    mf_stmt_bind_text(stmt, 11, symbol ? symbol : "");
    mf_stmt_bind_text(stmt, 12, quote_source ? quote_source : "");

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
asset_repo_update_basic(mf_db_t*    db,
                        int64_t     user_id,
                        int64_t     id,
                        const char* name,
                        const char* account_no,
                        double      current_value,
                        const char* currency,
                        const char* note,
                        const char* symbol,
                        const char* quote_source)
{
    if (!db) {
        return -1;
    }
    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(db,
                             "UPDATE assets SET "
                             "name = ?, account_no = ?, current_value = ?, currency = ?, note = ?, "
                             "symbol = ?, quote_source = ?, updated_at = CURRENT_TIMESTAMP "
                             "WHERE id = ? AND user_id = ?;",
                             &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    mf_stmt_bind_text(stmt, 1, name ? name : "");
    mf_stmt_bind_text(stmt, 2, account_no ? account_no : "");
    mf_stmt_bind_double(stmt, 3, current_value);
    mf_stmt_bind_text(stmt, 4, currency && currency[0] ? currency : "CNY");
    mf_stmt_bind_text(stmt, 5, note ? note : "");
    mf_stmt_bind_text(stmt, 6, symbol ? symbol : "");
    mf_stmt_bind_text(stmt, 7, quote_source ? quote_source : "");
    mf_stmt_bind_int64(stmt, 8, id);
    mf_stmt_bind_int64(stmt, 9, user_id);

    int64_t aff = 0;
    rc = mf_stmt_execute(stmt, &aff);
    mf_stmt_close(stmt);
    return (rc == 0 && aff > 0) ? 0 : -1;
}

int
asset_repo_update_position(mf_db_t* db,
                           int64_t  user_id,
                           int64_t  id,
                           double   quantity,
                           double   cost_basis,
                           double   net_value,
                           double   current_value)
{
    if (!db) {
        return -1;
    }
    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(db,
                             "UPDATE assets SET "
                             "quantity = ?, cost_basis = ?, net_value = ?, current_value = ?, "
                             "updated_at = CURRENT_TIMESTAMP "
                             "WHERE id = ? AND user_id = ?;",
                             &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    mf_stmt_bind_double(stmt, 1, quantity);
    mf_stmt_bind_double(stmt, 2, cost_basis);
    mf_stmt_bind_double(stmt, 3, net_value);
    mf_stmt_bind_double(stmt, 4, current_value);
    mf_stmt_bind_int64(stmt, 5, id);
    mf_stmt_bind_int64(stmt, 6, user_id);

    int64_t aff = 0;
    rc = mf_stmt_execute(stmt, &aff);
    mf_stmt_close(stmt);
    return (rc == 0 && aff > 0) ? 0 : -1;
}

int
asset_repo_delete(mf_db_t* db, int64_t user_id, int64_t id)
{
    if (!db) {
        return -1;
    }
    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(db, "DELETE FROM assets WHERE id = ? AND user_id = ?;", &stmt);
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
