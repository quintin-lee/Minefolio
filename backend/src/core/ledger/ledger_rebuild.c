#include "core/ledger/ledger_internal.h"
#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * 状态重算与事件溯源重建 (State Rebuild / Event Sourcing Engine)
 * ========================================================================= */

/**
 * @brief Replay chronological buy/sell transaction rows onto position state.
 * @return 0 on success, -1 on arithmetic/lot failure.
 */
static int
replay_position_transactions(csilk_db_pool_t* pool,
                             int64_t          user_id,
                             int64_t          asset_id,
                             currency_t       cur,
                             bool             is_fifo,
                             int64_t          exclude_tx_id,
                             csilk_json_t*    tx_rows,
                             quantity_t*      curr_qty,
                             money_t*         curr_cost,
                             price_t*         latest_price,
                             money_t*         cum_realized)
{
    size_t n = csilk_json_array_size(tx_rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(tx_rows, i);
        int64_t       tx_id = db_get_int(r, "id");
        if (exclude_tx_id > 0 && tx_id == exclude_tx_id) {
            continue;
        }

        const char* type = csilk_json_get_string(r, "transaction_type");
        price_t     p = db_get_price(r, "price_per_unit", cur);
        quantity_t  q = db_get_quantity(r, "quantity");
        money_t     f = db_get_money(r, "fee", cur);
        const char* tx_date = csilk_json_get_string(r, "transaction_date");
        if (!tx_date || !tx_date[0]) {
            tx_date = "1970-01-01 00:00:00";
        }

        if (type && strcmp(type, "buy") == 0) {
            if (ledger_calc_buy_position(*curr_qty, *curr_cost, q, p, f, curr_qty, curr_cost) !=
                0) {
                return -1;
            }
            if (is_fifo) {
                money_t buy_amt = db_get_money(r, "amount", cur);
                if (ledger_internal_create_buy_lot(
                        pool, user_id, asset_id, tx_id, tx_date, buy_amt, p, q, f) <= 0) {
                    return -1;
                }
            }
            if (!decimal_is_zero(p.unit_price)) {
                *latest_price = p;
            }
        } else if (type && strcmp(type, "sell") == 0) {
            money_t cost_red, real_pnl;
            int     rc;
            if (is_fifo) {
                rc = ledger_calc_sell_position_fifo(
                    pool, user_id, asset_id, q, p, f, curr_qty, curr_cost, &cost_red, &real_pnl);
                if (rc != 0) {
                    // Legacy FIFO rows may lack lot records: fall back to weighted average.
                    rc = ledger_calc_sell_position(
                        *curr_qty, *curr_cost, q, p, f, curr_qty, curr_cost, &cost_red, &real_pnl);
                }
            } else {
                rc = ledger_calc_sell_position(
                    *curr_qty, *curr_cost, q, p, f, curr_qty, curr_cost, &cost_red, &real_pnl);
            }
            if (rc != 0) {
                return -1;
            }
            if (money_add(*cum_realized, real_pnl, cum_realized) != DECIMAL_OK) {
                return -1;
            }
        }
    }
    return 0;
}

int
ledger_rebuild_position_ex(csilk_db_pool_t*         pool,
                           int64_t                  user_id,
                           int64_t                  asset_id,
                           ledger_position_state_t* out_state,
                           int64_t                  exclude_tx_id)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return -1;
    }
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_ledger_rebuild_pos", &scope) != 0) {
        return -1;
    }

    int64_t owner_id = user_id;
    if (ledger_internal_resolve_asset_owner(pool, asset_id, user_id, true, &owner_id, NULL) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }

    char uid_str[32], ast_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)owner_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);

    // 1. Fetch asset metadata, quantity, cost_basis, net_value & currency
    csilk_json_t* a_rows = csilk_db_query_param_json(
        pool,
        "SELECT a.currency, a.quantity, a.cost_basis, a.net_value, a.current_value, "
        "a.cost_method, c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id=c.id WHERE a.id=? AND a.user_id=?",
        (const char*[]){ast_str, uid_str, NULL});
    if (!a_rows || csilk_json_array_size(a_rows) == 0) {
        if (a_rows) {
            csilk_json_free(a_rows);
        }
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    const csilk_json_t* a_obj = csilk_json_array_get(a_rows, 0);
    const char*         atype_str = csilk_json_get_string(a_obj, "asset_type");
    bool                is_inv =
        (atype_str && (strcmp(atype_str, "stock") == 0 || strcmp(atype_str, "fund") == 0 ||
                       strcmp(atype_str, "bond") == 0 || strcmp(atype_str, "crypto") == 0));
    if (!is_inv) {
        csilk_json_free(a_rows);
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    const char* cur_str = csilk_json_get_string(a_obj, "currency");
    currency_t  cur = currency_from_str(cur_str);
    quantity_t  orig_qty = db_get_quantity(a_obj, "quantity");
    money_t     orig_cost = db_get_money(a_obj, "cost_basis", cur);
    price_t     orig_net = db_get_price(a_obj, "net_value", cur);
    const char* cm_str = csilk_json_get_string(a_obj, "cost_method");
    bool        is_fifo = (cm_str && strcmp(cm_str, "fifo") == 0);
    csilk_json_free(a_rows);

    // 2. Check for latest checkpoint snapshot (Two-stage incremental rebuild)
    // 注意：FIFO 资产的批次剩余量未完整物化到无批次断点，恢复快照会破坏 transaction_lots 状态，
    // 因此 FIFO 资产（除非显式无快照）始终执行全量批次重放。
    csilk_json_t* snap = NULL;
    if (exclude_tx_id <= 0 && !is_fifo) {
        ledger_snapshot_get_latest(pool, owner_id, asset_id, NULL, &snap);
    }
    bool had_snapshot = (snap != NULL);

    if (is_fifo) {
        tx_lots_delete_by_asset(pool, owner_id, asset_id);
    }

    quantity_t curr_qty;
    money_t    curr_cost;
    price_t    latest_price;
    money_t    cum_realized;

    csilk_json_t* tx_rows = NULL;

    if (had_snapshot) {
        curr_qty = db_get_quantity(snap, "quantity");
        curr_cost = db_get_money(snap, "cost_basis", cur);
        latest_price = db_get_price(snap, "net_value", cur);
        cum_realized = db_get_money(snap, "realized_pnl", cur);

        const char* ckpt_date = csilk_json_get_string(snap, "tx_checkpoint_date");
        if (!ckpt_date || !ckpt_date[0]) {
            ckpt_date = csilk_json_get_string(snap, "snapshot_date");
        }
        if (!ckpt_date) {
            ckpt_date = "";
        }
        int64_t ckpt_id = db_get_int(snap, "tx_checkpoint_id");
        char    ckpt_id_str[32];
        snprintf(ckpt_id_str, sizeof(ckpt_id_str), "%lld", (long long)ckpt_id);

        const char* sub_params[] = {ast_str, ckpt_date, ckpt_date, ckpt_id_str, NULL};
        tx_rows = csilk_db_query_param_json(
            pool,
            "SELECT id, transaction_type, amount, price_per_unit, quantity, fee, transaction_date "
            "FROM transactions WHERE asset_id=? AND transaction_type IN ('buy', "
            "'sell') "
            "AND (transaction_date > ? OR (transaction_date = ? AND id > ?)) "
            "ORDER BY transaction_date ASC, id ASC",
            sub_params);

        csilk_json_free(snap);
    } else {
        curr_qty = quantity_zero();
        curr_cost = money_zero(cur);
        latest_price = price_zero(cur);
        cum_realized = money_zero(cur);

        tx_rows = csilk_db_query_param_json(
            pool,
            "SELECT id, transaction_type, amount, price_per_unit, quantity, fee, transaction_date "
            "FROM transactions WHERE asset_id=? AND transaction_type IN ('buy', "
            "'sell') "
            "ORDER BY transaction_date ASC, id ASC",
            (const char*[]){ast_str, NULL});
    }

    if (tx_rows && csilk_json_array_size(tx_rows) > 0) {
        int replay_rc = replay_position_transactions(pool,
                                                     owner_id,
                                                     asset_id,
                                                     cur,
                                                     is_fifo,
                                                     exclude_tx_id,
                                                     tx_rows,
                                                     &curr_qty,
                                                     &curr_cost,
                                                     &latest_price,
                                                     &cum_realized);
        csilk_json_free(tx_rows);
        if (replay_rc != 0) {
            db_tx_scope_rollback(pool, &scope);
            return -1;
        }
    } else {
        if (tx_rows) {
            csilk_json_free(tx_rows);
        }
        // No transactions found: if no snapshot was present, preserve baseline initial quantity and
        // cost basis
        if (!had_snapshot && exclude_tx_id <= 0) {
            curr_qty = orig_qty;
            curr_cost = orig_cost;
            latest_price = orig_net;
        }
    }

    if (decimal_is_zero(latest_price.unit_price) && !decimal_is_zero(orig_net.unit_price)) {
        latest_price = orig_net;
    }

    money_t curr_mkt_val;
    if (price_times_quantity(latest_price, curr_qty, &curr_mkt_val) != DECIMAL_OK) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }

    money_t curr_unrealized;
    if (ledger_calc_unrealized_pnl(curr_qty, latest_price, curr_cost, &curr_unrealized) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }

    // 3. Update assets table with rebuilt state
    char qty_str[64], cost_str[64], nv_str[64], cv_str[64];
    quantity_to_string_fixed(curr_qty, 8, qty_str, sizeof(qty_str));
    money_to_string(curr_cost, cost_str, sizeof(cost_str));
    price_to_string(latest_price, nv_str, sizeof(nv_str));
    money_to_string(curr_mkt_val, cv_str, sizeof(cv_str));

    const char*   upd_params[] = {qty_str, cost_str, nv_str, cv_str, ast_str, uid_str, NULL};
    csilk_json_t* upd_res =
        csilk_db_query_param_json(pool,
                                  "UPDATE assets SET quantity=?, cost_basis=?, net_value=?, "
                                  "current_value=?, updated_at=CURRENT_TIMESTAMP "
                                  "WHERE id=? AND user_id=?",
                                  upd_params);
    if (!upd_res) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    csilk_json_free(upd_res);

    if (out_state) {
        out_state->asset_id = asset_id;
        out_state->quantity = curr_qty;
        out_state->cost_basis = curr_cost;
        out_state->net_value = latest_price;
        out_state->current_value = curr_mkt_val;
        out_state->realized_pnl = cum_realized;
        out_state->unrealized_pnl = curr_unrealized;
    }
    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    return 0;
}

int
ledger_rebuild_position(csilk_db_pool_t*         pool,
                        int64_t                  user_id,
                        int64_t                  asset_id,
                        ledger_position_state_t* out_state)
{
    return ledger_rebuild_position_ex(pool, user_id, asset_id, out_state, 0);
}

int
ledger_rebuild_account(csilk_db_pool_t*        pool,
                       int64_t                 user_id,
                       int64_t                 asset_id,
                       ledger_account_state_t* out_state)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return -1;
    }
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_ledger_rebuild_acc", &scope) != 0) {
        return -1;
    }

    int64_t owner_id = user_id;
    if (ledger_internal_resolve_asset_owner(pool, asset_id, user_id, true, &owner_id, NULL) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }

    char uid_str[32], ast_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)owner_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);

    // 1. Fetch asset currency, baseline current_value & category liability flag
    csilk_json_t* a_rows =
        csilk_db_query_param_json(pool,
                                  "SELECT a.currency, a.current_value, c.asset_type FROM assets a "
                                  "JOIN categories c ON a.category_id=c.id "
                                  "WHERE a.id=? AND a.user_id=?",
                                  (const char*[]){ast_str, uid_str, NULL});

    if (!a_rows || csilk_json_array_size(a_rows) == 0) {
        if (a_rows) {
            csilk_json_free(a_rows);
        }
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    csilk_json_t* a_obj = csilk_json_array_get(a_rows, 0);
    const char*   cur_str = csilk_json_get_string(a_obj, "currency");
    currency_t    cur = currency_from_str(cur_str);
    money_t       orig_current_val = db_get_money(a_obj, "current_value", cur);
    csilk_json_free(a_rows);

    // 2. Replay all balance audit logs in chronological order
    csilk_json_t* log_rows = csilk_db_query_param_json(
        pool,
        "SELECT delta, balance_after, source_type FROM asset_balance_logs WHERE asset_id=? ORDER "
        "BY created_at ASC, id ASC",
        (const char*[]){ast_str, NULL});

    money_t cum_bal = money_zero(cur);
    int64_t tx_count = 0;

    if (log_rows && csilk_json_array_size(log_rows) > 0) {
        size_t n = csilk_json_array_size(log_rows);
        tx_count = (int64_t)n;

        // Check if the earliest log is an explicit 'init' baseline
        csilk_json_t* first_row = csilk_json_array_get(log_rows, 0);
        const char*   first_source = csilk_json_get_string(first_row, "source_type");
        if (!first_source || strcmp(first_source, "init") != 0) {
            // Legacy database or pre-existing asset without explicit init log:
            // Calculate base balance before first recorded transaction: base = balance_after -
            // delta
            money_t first_after = db_get_money(first_row, "balance_after", cur);
            money_t first_delta = db_get_money(first_row, "delta", cur);
            if (money_sub(first_after, first_delta, &cum_bal) != DECIMAL_OK) {
                csilk_json_free(log_rows);
                db_tx_scope_rollback(pool, &scope);
                return -1;
            }
        }

        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(log_rows, i);
            money_t       d = db_get_money(r, "delta", cur);
            if (money_add(cum_bal, d, &cum_bal) != DECIMAL_OK) {
                csilk_json_free(log_rows);
                db_tx_scope_rollback(pool, &scope);
                return -1;
            }
        }
        csilk_json_free(log_rows);
    } else {
        if (log_rows) {
            csilk_json_free(log_rows);
        }
        // No logs found: preserve original current_value
        cum_bal = orig_current_val;
    }

    // 3. Update assets current_value
    char bal_str[64];
    money_to_string(cum_bal, bal_str, sizeof(bal_str));
    const char*   upd_params[] = {bal_str, ast_str, uid_str, NULL};
    csilk_json_t* upd_res = csilk_db_query_param_json(
        pool,
        "UPDATE assets SET current_value=?, updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=?",
        upd_params);
    if (!upd_res) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    csilk_json_free(upd_res);

    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }

    if (out_state) {
        out_state->asset_id = asset_id;
        out_state->balance = cum_bal;
        out_state->tx_count = tx_count;
    }
    return 0;
}

int
ledger_rebuild_portfolio(csilk_db_pool_t* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_ledger_rebuild_port", &scope) != 0) {
        return -1;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* assets = csilk_db_query_param_json(pool,
                                                     "SELECT a.id, c.asset_type FROM assets a "
                                                     "JOIN categories c ON a.category_id=c.id "
                                                     "WHERE a.user_id=?",
                                                     (const char*[]){uid_str, NULL});

    if (!assets) {
        if (db_tx_scope_commit(pool, &scope) != 0) {
            db_tx_scope_rollback(pool, &scope);
            return -1;
        }
        return 0;
    }

    size_t n = csilk_json_array_size(assets);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(assets, i);
        int64_t       aid = db_get_int(r, "id");
        const char*   atype = csilk_json_get_string(r, "asset_type");
        bool is_inv = (atype && (strcmp(atype, "stock") == 0 || strcmp(atype, "fund") == 0 ||
                                 strcmp(atype, "bond") == 0 || strcmp(atype, "crypto") == 0));
        int  reb_rc = is_inv ? ledger_rebuild_position(pool, user_id, aid, NULL)
                             : ledger_rebuild_account(pool, user_id, aid, NULL);
        if (reb_rc != 0) {
            /* An individual asset may have an inconsistent historical stream
             * (e.g. a sell whose supporting buy was reversed). Skip it so the
             * portfolio rebuild remains bounded; the asset keeps its last
             * known state. */
            CSILK_LOG_W("ledger_rebuild_portfolio: asset %lld rebuild failed rc=%d, skipping",
                        (long long)aid,
                        reb_rc);
            continue;
        }
    }
    csilk_json_free(assets);
    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    return 0;
}
