#include "core/ledger/ledger_internal.h"
#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"
#include "common/balance.h"
#include "common/tx_types.h"
#include "common/ledger_utils.h"
#include "services/market/exchange_rate_service.h"
#include "csilk/csilk.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* =========================================================================
 * 内部共享与局部辅助函数 (Internal Helpers)
 * ========================================================================= */

int
ledger_internal_resolve_asset_owner(csilk_db_pool_t* pool,
                                    int64_t          asset_id,
                                    int64_t          caller_user_id,
                                    bool             require_write,
                                    int64_t*         out_owner_id,
                                    bool*            out_is_fifo)
{
    if (!pool || asset_id <= 0 || caller_user_id <= 0) {
        return -1;
    }
    char ast_str[32];
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);

    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT user_id, ledger_id, cost_method FROM assets WHERE id=?",
        (const char*[]){ast_str, NULL});
    if (!rows || csilk_json_array_size(rows) == 0) {
        if (rows) {
            csilk_json_free(rows);
        }
        return -1;
    }
    const csilk_json_t* r = csilk_json_array_get(rows, 0);
    int64_t             owner_id = db_get_int(r, "user_id");
    int64_t             ledger_id = db_get_int(r, "ledger_id");
    const char*         cm = csilk_json_get_string(r, "cost_method");
    bool                is_fifo = (cm && strcmp(cm, "fifo") == 0);
    csilk_json_free(rows);

    if (owner_id != caller_user_id) {
        if (ledger_id <= 0) {
            return -1;
        }
        char role[32] = {0};
        if (!ledger_get_user_role(pool, ledger_id, caller_user_id, role, sizeof(role)) ||
            !role[0]) {
            return -1;
        }
        if (require_write && strcmp(role, "owner") != 0 && strcmp(role, "editor") != 0) {
            return -1;
        }
    }

    if (out_owner_id) {
        *out_owner_id = owner_id;
    }
    if (out_is_fifo) {
        *out_is_fifo = is_fifo;
    }
    return 0;
}

bool
ledger_internal_asset_is_fifo(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    bool is_fifo = false;
    if (ledger_internal_resolve_asset_owner(pool, asset_id, user_id, false, NULL, &is_fifo) != 0) {
        return false;
    }
    return is_fifo;
}

int64_t
ledger_internal_create_buy_lot(csilk_db_pool_t* pool,
                               int64_t          user_id,
                               int64_t          asset_id,
                               int64_t          tx_id,
                               const char*      tx_date,
                               money_t          amount,
                               price_t          price,
                               quantity_t       qty,
                               money_t          fee)
{
    money_t buy_gross = amount;
    if (money_is_zero(buy_gross) && !decimal_is_zero(price.unit_price)) {
        if (price_times_quantity(price, qty, &buy_gross) != DECIMAL_OK) {
            return -1;
        }
    }
    money_t buy_total;
    if (money_add(buy_gross, fee, &buy_total) != DECIMAL_OK) {
        return -1;
    }
    if (quantity_is_zero(qty) || quantity_is_negative(qty)) {
        return -1;
    }

    price_t cost_per_unit;
    if (money_div_quantity(buy_total, qty, 8, ROUND_HALF_UP, &cost_per_unit) != DECIMAL_OK) {
        return -1;
    }

    return tx_lot_create(pool, user_id, asset_id, tx_id, tx_date, qty, cost_per_unit, fee);
}

/**
 * @brief Format current local timestamp in portable SQL datetime format.
 */
static void
format_now_timestamp(char* buf, size_t cap)
{
    time_t    now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &tmv);
}

/**
 * @brief Derive direction, linked direction, and source_type from transaction metadata.
 */
static void
derive_tx_directions(const ledger_tx_t* tx,
                     const char*        tx_type_str,
                     const char**       out_dir,
                     const char**       out_ldir,
                     const char**       out_src_type)
{
    const tx_type_t* ttype = tx_type_lookup(tx_type_str);
    *out_dir = (ttype && ttype->stat_dir) ? ttype->stat_dir : "in";
    *out_ldir = (tx->linked_asset_id > 0 && ttype && ttype->linked_dir) ? ttype->linked_dir : "";
    *out_src_type = (tx->type == LEDGER_TX_DEPOSIT || tx->type == LEDGER_TX_TRANSFER_IN ||
                     tx->type == LEDGER_TX_INTEREST || tx->type == LEDGER_TX_DIVIDEND)
                        ? "income"
                        : "expense";
}

/**
 * @brief Compute forward balance delta applied to linked funding asset.
 */
static money_t
compute_linked_delta(
    ledger_tx_type_t tx_type, const char* type_str, money_t amount, price_t price, quantity_t qty)
{
    if (tx_type == LEDGER_TX_BUY || (type_str && strcmp(type_str, "buy") == 0)) {
        return money_neg(amount);
    }
    if (tx_type == LEDGER_TX_SELL || (type_str && strcmp(type_str, "sell") == 0)) {
        return amount;
    }
    money_t tdelta = tx_delta_m(type_str, amount, price, qty);
    return tx_effective_ldelta_m(type_str, amount, tdelta);
}

/**
 * @brief Insert or update the parent transactions row inside an active savepoint scope.
 */
static int
upsert_parent_tx_row(csilk_db_pool_t* pool,
                     ledger_tx_t*     tx,
                     const char*      tx_type_str,
                     const char*      effective_date,
                     const char*      uid_str,
                     const char*      ast_str,
                     const char*      last_str,
                     const char*      cat_str,
                     const char*      lid_str,
                     const char*      amt_buf,
                     const char*      price_buf,
                     const char*      qty_buf,
                     const char*      fee_buf,
                     const char*      cur_str,
                     const char*      fx_buf)
{
    const char* dir = NULL;
    const char* ldir = NULL;
    const char* src_type = NULL;
    derive_tx_directions(tx, tx_type_str, &dir, &ldir, &src_type);

    if (tx->id <= 0) {
        const char* parent_tx_str = "";
        char        ptx_buf[32];
        if (tx->parent_tx_id > 0) {
            snprintf(ptx_buf, sizeof(ptx_buf), "%lld", (long long)tx->parent_tx_id);
            parent_tx_str = ptx_buf;
        }

        const char* ins_params[] = {uid_str,
                                    ast_str,
                                    last_str,
                                    cat_str,
                                    src_type,
                                    tx_type_str,
                                    dir,
                                    ldir,
                                    amt_buf,
                                    price_buf,
                                    qty_buf,
                                    fee_buf,
                                    cur_str,
                                    fx_buf,
                                    effective_date,
                                    tx->note ? tx->note : "",
                                    parent_tx_str,
                                    lid_str,
                                    ast_str,
                                    uid_str,
                                    NULL};

        csilk_json_t* ins_res = csilk_db_query_param_json(
            pool,
            "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, "
            "source_type, transaction_type, direction, linked_direction, "
            "amount, price_per_unit, quantity, fee, currency, fx_rate_to_cny, "
            "transaction_date, note, parent_tx_id, ledger_id) "
            "VALUES (?, ?, NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), NULLIF(CAST(NULLIF(?, '') AS "
            "BIGINT), 0), ?, ?, "
            "?, NULLIF(?, ''), ?, ?, ?, ?, ?, ?, "
            "?, ?, NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), "
            "COALESCE(NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), (SELECT ledger_id FROM assets "
            "WHERE id = ?), "
            "(SELECT id FROM ledgers WHERE owner_id = ? AND is_default = 1))) "
            "RETURNING id",
            ins_params);

        if (!ins_res || csilk_json_array_size(ins_res) == 0) {
            if (ins_res) {
                csilk_json_free(ins_res);
            }
            return -1;
        }
        tx->id = db_get_int(csilk_json_array_get(ins_res, 0), "id");
        csilk_json_free(ins_res);
        return 0;
    }

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)tx->id);

    const char* upd_params[] = {ast_str,
                                tx_type_str,
                                dir,
                                ldir,
                                amt_buf,
                                price_buf,
                                qty_buf,
                                fee_buf,
                                cur_str,
                                fx_buf,
                                effective_date,
                                tx->note ? tx->note : "",
                                cat_str,
                                src_type,
                                last_str,
                                id_str,
                                uid_str,
                                NULL};

    csilk_json_t* upd_res = csilk_db_query_param_json(
        pool,
        "UPDATE transactions SET "
        "asset_id=?, transaction_type=?, direction=?, linked_direction=NULLIF(?, ''), "
        "amount=?, price_per_unit=?, quantity=?, fee=?, currency=?, fx_rate_to_cny=?, "
        "transaction_date=?, note=?, category_id=NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), "
        "source_type=?, "
        "linked_asset_id=NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0) "
        "WHERE id=? AND user_id=? RETURNING id",
        upd_params);

    if (!upd_res || csilk_json_array_size(upd_res) == 0) {
        if (upd_res) {
            csilk_json_free(upd_res);
        }
        return -1;
    }
    csilk_json_free(upd_res);
    return 0;
}

/**
 * @brief Insert child fee transaction row and debit fee from funding/target asset.
 */
static int
insert_and_apply_fee_child(csilk_db_pool_t*   pool,
                           const ledger_tx_t* tx,
                           const char*        effective_date,
                           const char*        uid_str,
                           const char*        ast_str,
                           const char*        cat_str,
                           const char*        lid_str,
                           const char*        fee_buf,
                           const char*        cur_str,
                           const char*        fx_buf,
                           double             fx_rate_to_cny)
{
    int64_t fee_target_asset = (tx->linked_asset_id > 0) ? tx->linked_asset_id : tx->asset_id;
    char    tx_id_str[32], fee_target_str[32];
    snprintf(tx_id_str, sizeof(tx_id_str), "%lld", (long long)tx->id);
    snprintf(fee_target_str, sizeof(fee_target_str), "%lld", (long long)fee_target_asset);

    const char*   fee_note = (tx->note && tx->note[0]) ? tx->note : "fee";
    const char*   fee_params[] = {uid_str,
                                  ast_str,
                                  fee_target_str,
                                  cat_str,
                                  "expense",
                                  "fee",
                                  "out",
                                  fee_buf,
                                  "0.00000000",
                                  "0.00000000",
                                  "0.00000000",
                                  cur_str,
                                  fx_buf,
                                  effective_date,
                                  fee_note,
                                  tx_id_str,
                                  lid_str,
                                  NULL};
    csilk_json_t* fee_res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, "
        "source_type, transaction_type, direction, linked_direction, "
        "amount, price_per_unit, quantity, fee, currency, fx_rate_to_cny, "
        "transaction_date, note, parent_tx_id, ledger_id) "
        "VALUES (?, ?, NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), NULLIF(CAST(NULLIF(?, '') AS "
        "BIGINT), 0), ?, ?, "
        "?, NULL, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, "
        "NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0))",
        fee_params);
    if (!fee_res) {
        CSILK_LOG_E("insert_and_apply_fee_child: failed to insert child fee row for tx_id=%lld",
                    (long long)tx->id);
        return -1;
    }
    csilk_json_free(fee_res);

    money_t neg_fee = money_neg(tx->fee);
    return balance_apply_delta_m_fx_at(pool,
                                       fee_target_asset,
                                       tx->user_id,
                                       neg_fee,
                                       fx_rate_to_cny,
                                       effective_date,
                                       "transaction_fee",
                                       tx->id,
                                       fee_note);
}

/**
 * @brief Reverse balance deltas for all child fee rows and delete them.
 */
static int
reverse_fee_children(csilk_db_pool_t* pool,
                     int64_t          user_id,
                     int64_t          tx_id,
                     currency_t       cur,
                     double           fx_rate_to_cny,
                     const char*      tx_date)
{
    csilk_json_t* fee_children = tx_child_fee_rows(pool, user_id, tx_id);
    if (!fee_children) {
        return 0;
    }

    size_t fee_cnt = csilk_json_array_size(fee_children);
    for (size_t i = 0; i < fee_cnt; i++) {
        csilk_json_t* frow = csilk_json_array_get(fee_children, i);
        int64_t       f_last = (int64_t)db_get_int(frow, "linked_asset_id");
        int64_t       f_target = (f_last > 0) ? f_last : (int64_t)db_get_int(frow, "asset_id");
        money_t       f_amt = db_get_money(frow, "amount", cur);
        if (f_target > 0 && !money_is_zero(f_amt)) {
            if (balance_apply_delta_m_fx_at(pool,
                                            f_target,
                                            user_id,
                                            f_amt,
                                            fx_rate_to_cny,
                                            tx_date,
                                            "transaction_fee_reversal",
                                            tx_id,
                                            "fee rollback") != 0) {
                csilk_json_free(fee_children);
                return -1;
            }
        }
    }
    csilk_json_free(fee_children);

    return (tx_delete_fee_children(pool, user_id, tx_id) < 0) ? -1 : 0;
}

static int
invalidate_asset_snapshot_safe(csilk_db_pool_t* pool,
                               int64_t          user_id,
                               int64_t          asset_id,
                               const char*      event_date)
{
    if (asset_id <= 0) {
        return 0;
    }
    char day_buf[32];
    if (event_date && strlen(event_date) >= 10) {
        snprintf(day_buf, sizeof(day_buf), "%.10s", event_date);
    } else {
        format_now_timestamp(day_buf, sizeof(day_buf));
        day_buf[10] = '\0';
    }
    return ledger_snapshot_invalidate_from(pool, user_id, asset_id, day_buf);
}

/**
 * @brief Apply or reverse a single daily expense balance delta inside a savepoint scope.
 */
static int
apply_expense_delta(csilk_db_pool_t* pool,
                    int64_t          user_id,
                    int64_t          asset_id,
                    money_t          delta,
                    const char*      source_type,
                    int64_t          expense_id,
                    const char*      note,
                    const char*      event_date,
                    const char*      scope_name)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return -1;
    }
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, scope_name, &scope) != 0) {
        return -1;
    }
    if (balance_apply_delta_m(pool, asset_id, user_id, delta, source_type, expense_id, note) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (invalidate_asset_snapshot_safe(pool, user_id, asset_id, event_date) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    return 0;
}

/**
 * @brief Apply or reverse paired transfer deltas inside a savepoint scope.
 */
static int
apply_transfer_deltas(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          from_asset_id,
                      int64_t          to_asset_id,
                      money_t          from_delta,
                      money_t          to_delta,
                      const char*      from_source,
                      const char*      to_source,
                      int64_t          transfer_id,
                      const char*      note,
                      const char*      event_date,
                      const char*      scope_name)
{
    if (!pool || user_id <= 0 || from_asset_id <= 0 || to_asset_id <= 0) {
        return -1;
    }
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, scope_name, &scope) != 0) {
        return -1;
    }
    if (balance_apply_delta_m_fx_at(pool,
                                    from_asset_id,
                                    user_id,
                                    from_delta,
                                    0.0,
                                    event_date,
                                    from_source,
                                    transfer_id,
                                    note) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (balance_apply_delta_m_fx_at(
            pool, to_asset_id, user_id, to_delta, 0.0, event_date, to_source, transfer_id, note) !=
        0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (invalidate_asset_snapshot_safe(pool, user_id, from_asset_id, event_date) != 0 ||
        invalidate_asset_snapshot_safe(pool, user_id, to_asset_id, event_date) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    return 0;
}

/* =========================================================================
 * 数据库事务操作算子 (Database Transaction Operators)
 * ========================================================================= */

int
ledger_apply_tx(csilk_db_pool_t* pool, ledger_tx_t* tx)
{
    if (!pool || !tx || tx->user_id <= 0 || tx->asset_id <= 0) {
        return -1;
    }
    if (tx->type == LEDGER_TX_UNKNOWN) {
        tx->type = ledger_tx_type_from_str(tx->type_str);
    }
    if (tx->type == LEDGER_TX_UNKNOWN) {
        return -1;
    }
    if (money_is_negative(tx->fee)) {
        CSILK_LOG_W("ledger_apply_tx: negative fee rejected user_id=%lld asset_id=%lld",
                    (long long)tx->user_id,
                    (long long)tx->asset_id);
        return -1;
    }

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_ledger_apply_tx", &scope) != 0) {
        return -1;
    }
    int64_t orig_tx_id = tx->id;

    char uid_str[32], ast_str[32], last_str[32], cat_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)tx->user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)tx->asset_id);
    snprintf(last_str, sizeof(last_str), "%lld", (long long)tx->linked_asset_id);
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)tx->category_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)tx->ledger_id);

    /* Portable "now" fallback for transaction_date (SQLite has no PG NOW(), and a
     * quoted 'datetime('now')' param would be stored as a literal string). */
    char now_fallback[32];
    format_now_timestamp(now_fallback, sizeof(now_fallback));
    const char* effective_date = tx->tx_date ? tx->tx_date : now_fallback;

    /* Historical FX: stamp the point-in-time CNY rate on the row and use that same
     * rate for every balance/position conversion so cross-currency transactions
     * never drift with today's rate. */
    double fx_rate =
        exchange_rate_get_historical_or_latest(pool, tx->amount.currency, effective_date);
    if (!isfinite(fx_rate) || fx_rate <= 0.0) {
        fx_rate = 0.0;
    }
    char fx_buf[32];
    snprintf(fx_buf, sizeof(fx_buf), "%.6f", fx_rate > 0.0 ? fx_rate : 1.0);

    char amt_buf[64], price_buf[64], qty_buf[64], fee_buf[64];
    money_to_string(tx->amount, amt_buf, sizeof(amt_buf));
    price_to_string(tx->price, price_buf, sizeof(price_buf));
    quantity_to_string_fixed(tx->quantity, 8, qty_buf, sizeof(qty_buf));
    money_to_string(tx->fee, fee_buf, sizeof(fee_buf));

    const char* cur_str = currency_code(&tx->amount.currency);
    const char* tx_type_str = tx->type_str ? tx->type_str : ledger_tx_type_to_str(tx->type);

    // 1. Insert or update parent transaction record
    if (upsert_parent_tx_row(pool,
                             tx,
                             tx_type_str,
                             effective_date,
                             uid_str,
                             ast_str,
                             last_str,
                             cat_str,
                             lid_str,
                             amt_buf,
                             price_buf,
                             qty_buf,
                             fee_buf,
                             cur_str,
                             fx_buf) != 0) {
        goto fail_rollback;
    }

    // 2. Position & Balance Updates
    bool    is_investment_tx = (tx->type == LEDGER_TX_BUY || tx->type == LEDGER_TX_SELL);
    money_t pos_delta = money_zero(tx->amount.currency);

    if (is_investment_tx) {
        int pos_rc = apply_position_fc_dated(pool,
                                             tx->asset_id,
                                             tx->user_id,
                                             tx_type_str,
                                             tx->amount,
                                             tx->fee,
                                             tx->price,
                                             tx->quantity,
                                             fx_rate,
                                             effective_date,
                                             tx->id,
                                             &pos_delta);
        if (pos_rc < 0) {
            goto fail_rollback;
        }

        // On BUY for FIFO asset, record the new transaction lot under asset owner_id
        int64_t pos_owner_id = tx->user_id;
        bool    is_fifo = false;
        if (tx->type == LEDGER_TX_BUY &&
            ledger_internal_resolve_asset_owner(
                pool, tx->asset_id, tx->user_id, true, &pos_owner_id, &is_fifo) == 0 &&
            is_fifo) {
            int64_t lot_id = ledger_internal_create_buy_lot(pool,
                                                            pos_owner_id,
                                                            tx->asset_id,
                                                            tx->id,
                                                            effective_date,
                                                            tx->amount,
                                                            tx->price,
                                                            tx->quantity,
                                                            tx->fee);
            if (lot_id <= 0) {
                goto fail_rollback;
            }
        }
    }

    // 3. Fee record generation if fee > 0 on any non-fee transaction
    if (money_is_positive(tx->fee) && tx->type != LEDGER_TX_FEE) {
        if (insert_and_apply_fee_child(pool,
                                       tx,
                                       effective_date,
                                       uid_str,
                                       ast_str,
                                       cat_str,
                                       lid_str,
                                       fee_buf,
                                       cur_str,
                                       fx_buf,
                                       fx_rate) != 0) {
            goto fail_rollback;
        }
    }

    // 4. Target Asset Balance Delta
    if (is_investment_tx) {
        if (!money_is_zero(pos_delta) && balance_apply_delta_m_fx_at(pool,
                                                                     tx->asset_id,
                                                                     tx->user_id,
                                                                     pos_delta,
                                                                     fx_rate,
                                                                     effective_date,
                                                                     "transaction",
                                                                     tx->id,
                                                                     tx->note) != 0) {
            goto fail_rollback;
        }
    } else if (tx->type == LEDGER_TX_FEE && tx->linked_asset_id > 0) {
        // Standalone fee with linked account: debited from linked funding asset only
    } else {
        money_t tdelta = tx_delta_m(tx_type_str, tx->amount, tx->price, tx->quantity);
        if (!money_is_zero(tdelta) && balance_apply_delta_m_fx_at(pool,
                                                                  tx->asset_id,
                                                                  tx->user_id,
                                                                  tdelta,
                                                                  fx_rate,
                                                                  effective_date,
                                                                  "transaction",
                                                                  tx->id,
                                                                  tx->note) != 0) {
            goto fail_rollback;
        }
    }

    // 5. Linked Funding Asset Balance Delta
    if (tx->linked_asset_id > 0) {
        money_t ldelta =
            compute_linked_delta(tx->type, tx_type_str, tx->amount, tx->price, tx->quantity);
        if (!money_is_zero(ldelta) && balance_apply_delta_m_fx_at(pool,
                                                                  tx->linked_asset_id,
                                                                  tx->user_id,
                                                                  ldelta,
                                                                  fx_rate,
                                                                  effective_date,
                                                                  "transaction_linked",
                                                                  tx->id,
                                                                  tx->note) != 0) {
            goto fail_rollback;
        }
    }

    if (invalidate_asset_snapshot_safe(pool, tx->user_id, tx->asset_id, effective_date) != 0 ||
        invalidate_asset_snapshot_safe(pool, tx->user_id, tx->linked_asset_id, effective_date) !=
            0) {
        goto fail_rollback;
    }

    if (db_tx_scope_commit(pool, &scope) != 0) {
        goto fail_rollback;
    }
    return 0;

fail_rollback:
    if (orig_tx_id <= 0) {
        tx->id = 0;
    }
    db_tx_scope_rollback(pool, &scope);
    return -1;
}

/**
 * @brief Legacy manual position rollback used only when checkpoint replay is
 *        inconsistent (e.g. removing a buy that supported a later sell).
 */
static int
ledger_reverse_position_fallback(csilk_db_pool_t* pool,
                                 int64_t          user_id,
                                 int64_t          asset_id,
                                 const char*      type,
                                 money_t          amount,
                                 money_t          fee,
                                 price_t          price,
                                 quantity_t       qty,
                                 currency_t       cur,
                                 double           fx_rate_to_cny,
                                 const char*      tx_date,
                                 money_t*         out_position_delta)
{
    int64_t pos_owner_id = user_id;
    bool    is_fifo = false;
    if (ledger_internal_resolve_asset_owner(
            pool, asset_id, user_id, true, &pos_owner_id, &is_fifo) != 0) {
        return 0;
    }

    char aid_str[32], uid_str[32];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)pos_owner_id);
    csilk_json_t* pos = csilk_db_query_param_json(
        pool,
        "SELECT a.quantity, a.cost_basis, a.net_value, a.current_value, a.currency, a.cost_method, "
        "c.asset_type FROM assets a JOIN categories c ON a.category_id=c.id WHERE a.id=? AND "
        "a.user_id=?",
        (const char*[]){aid_str, uid_str, NULL});
    if (!pos || csilk_json_array_size(pos) == 0) {
        if (pos) {
            csilk_json_free(pos);
        }
        return 0;
    }
    const csilk_json_t* pr = csilk_json_array_get(pos, 0);
    const char*         atype = csilk_json_get_string(pr, "asset_type");
    if (!atype || (strcmp(atype, "stock") != 0 && strcmp(atype, "fund") != 0 &&
                   strcmp(atype, "bond") != 0 && strcmp(atype, "crypto") != 0)) {
        csilk_json_free(pos);
        return 0;
    }

    if (!currency_equals(amount.currency, cur)) {
        double src_r = fx_rate_to_cny > 0.0
                           ? fx_rate_to_cny
                           : exchange_rate_get_historical_or_latest(pool, amount.currency, tx_date);
        double tgt_r = exchange_rate_get_historical_or_latest(pool, cur, tx_date);
        if (src_r > 0.0 && tgt_r > 0.0) {
            decimal_t f, prod, rounded;
            if (decimal_from_double(src_r / tgt_r, 6, &f) == DECIMAL_OK &&
                decimal_mul(amount.amount, f, &prod) == DECIMAL_OK &&
                decimal_round(prod, (int32_t)currency_precision(cur), ROUND_HALF_UP, &rounded) ==
                    DECIMAL_OK) {
                amount.amount = rounded;
                amount.currency = cur;
            } else {
                amount = exchange_rate_convert_m(amount, cur);
            }
        } else {
            amount = exchange_rate_convert_m(amount, cur);
        }
    }
    if (!currency_equals(fee.currency, cur)) {
        double src_r = fx_rate_to_cny > 0.0
                           ? fx_rate_to_cny
                           : exchange_rate_get_historical_or_latest(pool, fee.currency, tx_date);
        double tgt_r = exchange_rate_get_historical_or_latest(pool, cur, tx_date);
        if (src_r > 0.0 && tgt_r > 0.0) {
            decimal_t f, prod, rounded;
            if (decimal_from_double(src_r / tgt_r, 6, &f) == DECIMAL_OK &&
                decimal_mul(fee.amount, f, &prod) == DECIMAL_OK &&
                decimal_round(prod, (int32_t)currency_precision(cur), ROUND_HALF_UP, &rounded) ==
                    DECIMAL_OK) {
                fee.amount = rounded;
                fee.currency = cur;
            } else {
                fee = exchange_rate_convert_m(fee, cur);
            }
        } else {
            fee = exchange_rate_convert_m(fee, cur);
        }
    }
    price = balance_convert_price_fx_at(pool, price, cur, fx_rate_to_cny, tx_date);

    quantity_t old_qty = db_get_quantity(pr, "quantity");
    money_t    old_cost = db_get_money(pr, "cost_basis", cur);
    price_t    old_net = db_get_price(pr, "net_value", cur);
    money_t    old_current = db_get_money(pr, "current_value", cur);

    quantity_t new_qty;
    money_t    new_cost;
    price_t    new_net;

    if (strcmp(type, "buy") == 0) {
        if (quantity_cmp(old_qty, qty) > 0) {
            quantity_sub(old_qty, qty, &new_qty);
        } else {
            new_qty = quantity_zero();
        }
        money_t total_paid;
        money_add(amount, fee, &total_paid);
        if (money_cmp(old_cost, total_paid) > 0) {
            money_sub(old_cost, total_paid, &new_cost);
        } else {
            new_cost = money_zero(cur);
        }
        new_net = !quantity_is_zero(new_qty) ? old_net : price_zero(cur);
    } else if (strcmp(type, "sell") == 0) {
        quantity_add(old_qty, qty, &new_qty);
        money_t restored_cost;
        if (!quantity_is_zero(old_qty)) {
            decimal_t ratio;
            decimal_div(qty.units, old_qty.units, 12, ROUND_HALF_UP, &ratio);
            decimal_t rest_dec;
            decimal_mul(old_cost.amount, ratio, &rest_dec);
            uint8_t prec = currency_precision(cur);
            decimal_round(rest_dec, (int32_t)prec, ROUND_HALF_UP, &restored_cost.amount);
            restored_cost.currency = cur;
        } else {
            price_times_quantity(price, qty, &restored_cost);
        }
        money_add(old_cost, restored_cost, &new_cost);
        new_net = old_net;
    } else {
        csilk_json_free(pos);
        return 0;
    }

    money_t new_val;
    money_t delta_m;
    price_times_quantity(new_net, new_qty, &new_val);
    money_sub(new_val, old_current, &delta_m);

    if (out_position_delta) {
        *out_position_delta = delta_m;
    }

    char nq[64], nc[64], nv[64];
    quantity_to_string_fixed(new_qty, 8, nq, sizeof(nq));
    money_to_string(new_cost, nc, sizeof(nc));
    price_to_string_fixed(new_net, 8, nv, sizeof(nv));

    const char*   p[] = {nq, nc, nv, aid_str, uid_str, NULL};
    csilk_json_t* upd_res = csilk_db_query_param_json(
        pool,
        "UPDATE assets SET quantity=?, cost_basis=?, net_value=? WHERE id=? AND user_id=?",
        p);
    if (!upd_res) {
        csilk_json_free(pos);
        return -2;
    }
    csilk_json_free(upd_res);
    csilk_json_free(pos);
    return 0;
}

int
ledger_reverse_tx(csilk_db_pool_t* pool, int64_t user_id, int64_t tx_id)
{
    if (!pool || user_id <= 0 || tx_id <= 0) {
        return -1;
    }

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_ledger_reverse_tx", &scope) != 0) {
        return -1;
    }

    csilk_json_t* old_arr = tx_get_old(pool, user_id, tx_id);
    if (!old_arr || csilk_json_array_size(old_arr) == 0) {
        if (old_arr) {
            csilk_json_free(old_arr);
        }
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    csilk_json_t* old_tx = csilk_json_array_get(old_arr, 0);

    int64_t     asset_id = (int64_t)db_get_int(old_tx, "asset_id");
    int64_t     linked_asset_id = (int64_t)db_get_int(old_tx, "linked_asset_id");
    const char* type = csilk_json_get_string(old_tx, "transaction_type");
    const char* cur_str = csilk_json_get_string(old_tx, "currency");
    currency_t  cur = currency_from_str(cur_str);

    money_t    amount = db_get_money(old_tx, "amount", cur);
    money_t    fee = db_get_money(old_tx, "fee", cur);
    price_t    price = db_get_price(old_tx, "price_per_unit", cur);
    quantity_t qty = db_get_quantity(old_tx, "quantity");

    double fx_rate = db_get_num(old_tx, "fx_rate_to_cny");
    if (!isfinite(fx_rate) || fx_rate <= 0.0) {
        fx_rate = 0.0;
    } else if (cur_str && cur_str[0] && strcasecmp(cur_str, "CNY") != 0 &&
               strcasecmp(cur_str, "RMB") != 0 && fabs(fx_rate - 1.0) < 1e-9) {
        /* Legacy pre-fix rows (e.g. cross-currency transfers) defaulted fx_rate_to_cny to 1.0;
         * clear to 0.0 so reversal resolves the historical rate for transaction_date. */
        fx_rate = 0.0;
    }
    const char* rev_date = csilk_json_get_string(old_tx, "transaction_date");

    // 1. Query and reverse any fee children
    if (reverse_fee_children(pool, user_id, tx_id, cur, fx_rate, rev_date) != 0) {
        goto fail_rollback;
    }

    // 2. Position / Asset balance rollback
    bool    is_inv = (type && (strcmp(type, "buy") == 0 || strcmp(type, "sell") == 0));
    int64_t pos_owner_id = user_id;
    bool    is_fifo = false;
    if (is_inv && ledger_internal_resolve_asset_owner(
                      pool, asset_id, user_id, true, &pos_owner_id, &is_fifo) != 0) {
        goto fail_rollback;
    }

    if (is_inv) {
        if (is_fifo && strcmp(type, "buy") == 0) {
            tx_lots_delete_by_buy_tx(pool, pos_owner_id, tx_id);
        }
        if (ledger_rebuild_position_ex(pool, user_id, asset_id, NULL, tx_id) != 0) {
            /* Checkpoint replay can be inconsistent when the reversed
             * transaction supported later events (e.g. removing a buy that
             * covered a sell). Fall back to the legacy bounded rollback to
             * preserve the pinned regression behavior. */
            money_t fallback_delta;
            if (ledger_reverse_position_fallback(pool,
                                                 user_id,
                                                 asset_id,
                                                 type,
                                                 amount,
                                                 fee,
                                                 price,
                                                 qty,
                                                 cur,
                                                 fx_rate,
                                                 rev_date,
                                                 &fallback_delta) != 0) {
                goto fail_rollback;
            }
            if (!money_is_zero(fallback_delta) &&
                balance_apply_delta_m_fx_at(pool,
                                            asset_id,
                                            user_id,
                                            fallback_delta,
                                            fx_rate,
                                            rev_date,
                                            "transaction_reversal",
                                            tx_id,
                                            "position rollback (manual fallback)") != 0) {
                goto fail_rollback;
            }
        }
    } else if (type && strcmp(type, "fee") == 0 && linked_asset_id > 0) {
        // Standalone fee was debited from linked funding asset only
    } else {
        money_t tdelta = tx_delta_m(type, amount, price, qty);
        if (!money_is_zero(tdelta)) {
            money_t rev_tdelta = money_neg(tdelta);
            if (balance_apply_delta_m_fx_at(pool,
                                            asset_id,
                                            user_id,
                                            rev_tdelta,
                                            fx_rate,
                                            rev_date,
                                            "transaction_reversal",
                                            tx_id,
                                            "rollback") != 0) {
                goto fail_rollback;
            }
        }
    }

    // 3. Linked funding asset delta rollback
    if (linked_asset_id > 0) {
        ledger_tx_type_t ltype = ledger_tx_type_from_str(type);
        money_t          ldelta = money_neg(compute_linked_delta(ltype, type, amount, price, qty));
        if (!money_is_zero(ldelta) && balance_apply_delta_m_fx_at(pool,
                                                                  linked_asset_id,
                                                                  user_id,
                                                                  ldelta,
                                                                  fx_rate,
                                                                  rev_date,
                                                                  "transaction_linked_reversal",
                                                                  tx_id,
                                                                  "rollback") != 0) {
            goto fail_rollback;
        }
    }

    if (invalidate_asset_snapshot_safe(pool, user_id, asset_id, rev_date) != 0 ||
        invalidate_asset_snapshot_safe(pool, user_id, linked_asset_id, rev_date) != 0) {
        goto fail_rollback;
    }

    if (db_tx_scope_commit(pool, &scope) != 0) {
        goto fail_rollback;
    }
    csilk_json_free(old_arr);
    return 0;

fail_rollback:
    csilk_json_free(old_arr);
    db_tx_scope_rollback(pool, &scope);
    return -1;
}

int
ledger_apply_expense_dated(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           int64_t          asset_id,
                           money_t          amount,
                           int              is_income,
                           int64_t          expense_id,
                           const char*      note,
                           const char*      expense_date)
{
    money_t delta = is_income ? amount : money_neg(amount);
    return apply_expense_delta(pool,
                               user_id,
                               asset_id,
                               delta,
                               "daily_expense",
                               expense_id,
                               note,
                               expense_date,
                               "mf_ledger_apply_exp");
}

int
ledger_apply_expense(csilk_db_pool_t* pool,
                     int64_t          user_id,
                     int64_t          asset_id,
                     money_t          amount,
                     int              is_income,
                     int64_t          expense_id,
                     const char*      note)
{
    return ledger_apply_expense_dated(
        pool, user_id, asset_id, amount, is_income, expense_id, note, NULL);
}

int
ledger_reverse_expense_dated(csilk_db_pool_t* pool,
                             int64_t          user_id,
                             int64_t          asset_id,
                             money_t          amount,
                             int              is_income,
                             int64_t          expense_id,
                             const char*      note,
                             const char*      expense_date)
{
    money_t delta = is_income ? money_neg(amount) : amount;
    return apply_expense_delta(pool,
                               user_id,
                               asset_id,
                               delta,
                               "daily_expense_reversal",
                               expense_id,
                               note,
                               expense_date,
                               "mf_ledger_reverse_exp");
}

int
ledger_reverse_expense(csilk_db_pool_t* pool,
                       int64_t          user_id,
                       int64_t          asset_id,
                       money_t          amount,
                       int              is_income,
                       int64_t          expense_id,
                       const char*      note)
{
    return ledger_reverse_expense_dated(
        pool, user_id, asset_id, amount, is_income, expense_id, note, NULL);
}

int
ledger_apply_transfer_dated(csilk_db_pool_t* pool,
                            int64_t          user_id,
                            int64_t          from_asset_id,
                            int64_t          to_asset_id,
                            money_t          amount,
                            int64_t          transfer_id,
                            const char*      note,
                            const char*      transfer_date)
{
    return apply_transfer_deltas(pool,
                                 user_id,
                                 from_asset_id,
                                 to_asset_id,
                                 money_neg(amount),
                                 amount,
                                 "transfer_out",
                                 "transfer_in",
                                 transfer_id,
                                 note,
                                 transfer_date,
                                 "mf_ledger_apply_transfer");
}

int
ledger_apply_transfer(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          from_asset_id,
                      int64_t          to_asset_id,
                      money_t          amount,
                      int64_t          transfer_id,
                      const char*      note)
{
    return ledger_apply_transfer_dated(
        pool, user_id, from_asset_id, to_asset_id, amount, transfer_id, note, NULL);
}

int
ledger_reverse_transfer(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          from_asset_id,
                        int64_t          to_asset_id,
                        money_t          amount,
                        int64_t          transfer_id,
                        const char*      note)
{
    return apply_transfer_deltas(pool,
                                 user_id,
                                 from_asset_id,
                                 to_asset_id,
                                 amount,
                                 money_neg(amount),
                                 "transfer_out_reversal",
                                 "transfer_in_reversal",
                                 transfer_id,
                                 note,
                                 NULL,
                                 "mf_ledger_reverse_transfer");
}

static int
balance_rebuild_position_ex_hook(csilk_db_pool_t* pool,
                                 int64_t          user_id,
                                 int64_t          asset_id,
                                 int64_t          exclude_tx_id,
                                 currency_t*      out_currency)
{
    ledger_position_state_t state;
    int rc = ledger_rebuild_position_ex(pool, user_id, asset_id, &state, exclude_tx_id);
    if (rc != 0) {
        return rc;
    }
    if (out_currency) {
        *out_currency = state.current_value.currency;
    }
    return 0;
}

void
ledger_register_balance_hooks(void)
{
    static const mf_balance_hooks_t hooks = {
        .get_fx_to_cny_safe = exchange_rate_get_to_cny_safe,
        .get_fx_historical_or_latest = exchange_rate_get_historical_or_latest,
        .calc_sell_position = ledger_calc_sell_position,
        .calc_sell_position_fifo = ledger_calc_sell_position_fifo,
        .rebuild_position_ex = balance_rebuild_position_ex_hook,
    };
    balance_register_hooks(&hooks);
}
