#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"
#include "infrastructure/repositories/transaction_lot_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
ledger_snapshot_get_latest(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           int64_t          asset_id,
                           const char*      target_date,
                           csilk_json_t**   out_snapshot)
{
    if (!out_snapshot) {
        return -1;
    }
    *out_snapshot = NULL;
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return -1;
    }

    char uid_str[32], ast_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);

    csilk_json_t* res = NULL;
    if (target_date && target_date[0] != '\0') {
        const char* params[] = {uid_str, ast_str, target_date, NULL};
        res = csilk_db_query_param_json(
            pool,
            "SELECT id, user_id, asset_id, snapshot_date, quantity, cost_basis, net_value, "
            "current_value, realized_pnl, balance, tx_checkpoint_date, tx_checkpoint_id, "
            "lots_snapshot_json, created_at "
            "FROM ledger_snapshots "
            "WHERE user_id=? AND asset_id=? AND snapshot_date <= ? "
            "ORDER BY snapshot_date DESC, id DESC LIMIT 1",
            params);
    } else {
        const char* params[] = {uid_str, ast_str, NULL};
        res = csilk_db_query_param_json(
            pool,
            "SELECT id, user_id, asset_id, snapshot_date, quantity, cost_basis, net_value, "
            "current_value, realized_pnl, balance, tx_checkpoint_date, tx_checkpoint_id, "
            "lots_snapshot_json, created_at "
            "FROM ledger_snapshots "
            "WHERE user_id=? AND asset_id=? "
            "ORDER BY snapshot_date DESC, id DESC LIMIT 1",
            params);
    }

    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        *out_snapshot = NULL;
        return 0;
    }

    *out_snapshot = csilk_json_copy((csilk_json_t*)csilk_json_array_get(res, 0));
    csilk_json_free(res);
    return 0;
}

int
ledger_snapshot_save(csilk_db_pool_t*               pool,
                     int64_t                        user_id,
                     int64_t                        asset_id,
                     const char*                    snapshot_date,
                     const ledger_position_state_t* pos,
                     money_t                        balance,
                     const char*                    checkpoint_date,
                     int64_t                        checkpoint_id,
                     const char*                    lots_json)
{
    if (!pool || user_id <= 0 || asset_id <= 0 || !snapshot_date || !snapshot_date[0]) {
        return -1;
    }

    char uid_str[32], ast_str[32], ckpt_id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);
    snprintf(ckpt_id_str, sizeof(ckpt_id_str), "%lld", (long long)checkpoint_id);

    char qty_str[64] = "0.00000000";
    char cost_str[64] = "0.00";
    char nv_str[64] = "0.00";
    char cv_str[64] = "0.00";
    char rpnl_str[64] = "0.00";
    char bal_str[64] = "0.00";

    if (pos) {
        quantity_to_string_fixed(pos->quantity, 8, qty_str, sizeof(qty_str));
        money_to_string(pos->cost_basis, cost_str, sizeof(cost_str));
        price_to_string(pos->net_value, nv_str, sizeof(nv_str));
        money_to_string(pos->current_value, cv_str, sizeof(cv_str));
        money_to_string(pos->realized_pnl, rpnl_str, sizeof(rpnl_str));
    }
    money_to_string(balance, bal_str, sizeof(bal_str));

    const char* ckpt_date =
        (checkpoint_date && checkpoint_date[0]) ? checkpoint_date : snapshot_date;

    char* auto_lots = NULL;
    if (!lots_json) {
        csilk_json_t* cm_rows = csilk_db_query_param_json(
            pool,
            "SELECT cost_method FROM assets WHERE id=? AND user_id=?",
            (const char*[]){ast_str, uid_str, NULL});
        if (cm_rows && csilk_json_array_size(cm_rows) > 0) {
            const char* cm = csilk_json_get_string(csilk_json_array_get(cm_rows, 0), "cost_method");
            if (cm && strcmp(cm, "fifo") == 0) {
                auto_lots = tx_lots_serialize_open_json(pool, user_id, asset_id);
            }
        }
        if (cm_rows) {
            csilk_json_free(cm_rows);
        }
    }
    const char* lots_str = lots_json ? lots_json : (auto_lots ? auto_lots : "");

    const char* sql = "INSERT INTO ledger_snapshots ("
                      "user_id, asset_id, snapshot_date, quantity, cost_basis, net_value, "
                      "current_value, realized_pnl, balance, tx_checkpoint_date, tx_checkpoint_id, "
                      "lots_snapshot_json) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULLIF(?, '')) "
                      "ON CONFLICT(user_id, asset_id, snapshot_date) DO UPDATE SET "
                      "quantity = excluded.quantity, "
                      "cost_basis = excluded.cost_basis, "
                      "net_value = excluded.net_value, "
                      "current_value = excluded.current_value, "
                      "realized_pnl = excluded.realized_pnl, "
                      "balance = excluded.balance, "
                      "tx_checkpoint_date = excluded.tx_checkpoint_date, "
                      "tx_checkpoint_id = excluded.tx_checkpoint_id, "
                      "lots_snapshot_json = excluded.lots_snapshot_json, "
                      "created_at = CURRENT_TIMESTAMP";

    const char* params[] = {uid_str,
                            ast_str,
                            snapshot_date,
                            qty_str,
                            cost_str,
                            nv_str,
                            cv_str,
                            rpnl_str,
                            bal_str,
                            ckpt_date,
                            ckpt_id_str,
                            lots_str,
                            NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    if (auto_lots) {
        free(auto_lots);
    }
    if (!res) {
        return -1;
    }
    csilk_json_free(res);
    return 0;
}

int
ledger_snapshot_invalidate_from(csilk_db_pool_t* pool,
                                int64_t          user_id,
                                int64_t          asset_id,
                                const char*      from_date)
{
    if (!pool || user_id <= 0 || asset_id <= 0 || !from_date || strlen(from_date) < 10) {
        return -1;
    }

    char uid_str[32], ast_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);

    char from_day[16] = {0};
    snprintf(from_day, sizeof(from_day), "%.10s", from_date);

    const char* sql =
        "DELETE FROM ledger_snapshots WHERE user_id=? AND asset_id=? AND snapshot_date >= ?";
    const char*   params[] = {uid_str, ast_str, from_day, NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    if (!res) {
        return -1;
    }
    csilk_json_free(res);
    return 0;
}
