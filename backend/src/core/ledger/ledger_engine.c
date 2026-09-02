#include "core/ledger/ledger_engine.h"
#include "common/balance.h"
#include "common/tx_types.h"
#include "common/db.h"
#include "repositories/transaction_repo.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

ledger_tx_type_t
ledger_tx_type_from_str(const char* str)
{
    if (!str || !str[0]) {
        return LEDGER_TX_UNKNOWN;
    }
    if (strcasecmp(str, "buy") == 0) {
        return LEDGER_TX_BUY;
    }
    if (strcasecmp(str, "sell") == 0) {
        return LEDGER_TX_SELL;
    }
    if (strcasecmp(str, "deposit") == 0 || strcasecmp(str, "income") == 0) {
        return LEDGER_TX_DEPOSIT;
    }
    if (strcasecmp(str, "withdraw") == 0 || strcasecmp(str, "expense") == 0) {
        return LEDGER_TX_WITHDRAW;
    }
    if (strcasecmp(str, "transfer_in") == 0) {
        return LEDGER_TX_TRANSFER_IN;
    }
    if (strcasecmp(str, "transfer_out") == 0) {
        return LEDGER_TX_TRANSFER_OUT;
    }
    if (strcasecmp(str, "dividend") == 0) {
        return LEDGER_TX_DIVIDEND;
    }
    if (strcasecmp(str, "interest") == 0) {
        return LEDGER_TX_INTEREST;
    }
    if (strcasecmp(str, "fee") == 0) {
        return LEDGER_TX_FEE;
    }
    if (strcasecmp(str, "tax") == 0) {
        return LEDGER_TX_TAX;
    }
    if (strcasecmp(str, "adjustment") == 0) {
        return LEDGER_TX_ADJUSTMENT;
    }
    return LEDGER_TX_UNKNOWN;
}

const char*
ledger_tx_type_to_str(ledger_tx_type_t type)
{
    switch (type) {
    case LEDGER_TX_BUY:
        return "buy";
    case LEDGER_TX_SELL:
        return "sell";
    case LEDGER_TX_DEPOSIT:
        return "deposit";
    case LEDGER_TX_WITHDRAW:
        return "withdraw";
    case LEDGER_TX_TRANSFER_IN:
        return "transfer_in";
    case LEDGER_TX_TRANSFER_OUT:
        return "transfer_out";
    case LEDGER_TX_DIVIDEND:
        return "dividend";
    case LEDGER_TX_INTEREST:
        return "interest";
    case LEDGER_TX_FEE:
        return "fee";
    case LEDGER_TX_TAX:
        return "tax";
    case LEDGER_TX_ADJUSTMENT:
        return "adjustment";
    default:
        return "unknown";
    }
}

/* =========================================================================
 * 1. 纯数学计算算子 (Pure Mathematical Operators)
 * ========================================================================= */

int
ledger_calc_buy_position(quantity_t  prev_qty,
                         money_t     prev_cost,
                         quantity_t  buy_qty,
                         price_t     buy_price,
                         money_t     buy_fee,
                         quantity_t* out_qty,
                         money_t*    out_cost)
{
    if (!out_qty || !out_cost) {
        return -1;
    }
    if (quantity_is_negative(buy_qty)) {
        return -1;
    }

    // new_qty = prev_qty + buy_qty
    quantity_add(prev_qty, buy_qty, out_qty);

    // buy_amount = buy_price * buy_qty
    money_t buy_gross;
    price_times_quantity(buy_price, buy_qty, &buy_gross);

    // buy_total = buy_gross + buy_fee
    money_t buy_total;
    money_add(buy_gross, buy_fee, &buy_total);

    // new_cost = prev_cost + buy_total
    money_add(prev_cost, buy_total, out_cost);
    return 0;
}

int
ledger_calc_sell_position(quantity_t  prev_qty,
                          money_t     prev_cost,
                          quantity_t  sell_qty,
                          price_t     sell_price,
                          money_t     sell_fee,
                          quantity_t* out_qty,
                          money_t*    out_cost,
                          money_t*    out_cost_reduction,
                          money_t*    out_realized_pnl)
{
    if (!out_qty || !out_cost || !out_cost_reduction || !out_realized_pnl) {
        return -1;
    }
    if (quantity_is_negative(sell_qty) || quantity_cmp(sell_qty, prev_qty) > 0) {
        return -1; // Insufficient quantity
    }

    // Full sell check
    if (quantity_cmp(sell_qty, prev_qty) == 0) {
        *out_qty = quantity_zero();
        *out_cost_reduction = prev_cost;
        *out_cost = money_zero(prev_cost.currency);

        money_t sell_gross;
        price_times_quantity(sell_price, sell_qty, &sell_gross);
        money_t sell_net;
        money_sub(sell_gross, sell_fee, &sell_net);
        money_sub(sell_net, prev_cost, out_realized_pnl);
        return 0;
    }

    // Partial sell
    // ratio = sell_qty / prev_qty
    decimal_t ratio;
    decimal_div(sell_qty.units, prev_qty.units, 12, ROUND_HALF_UP, &ratio);

    // cost_reduction = prev_cost * ratio
    decimal_t red_dec;
    decimal_mul(prev_cost.amount, ratio, &red_dec);
    uint8_t prec = currency_precision(prev_cost.currency);
    decimal_round(red_dec, (int32_t)prec, ROUND_HALF_UP, &out_cost_reduction->amount);
    out_cost_reduction->currency = prev_cost.currency;

    // new_qty = prev_qty - sell_qty
    quantity_sub(prev_qty, sell_qty, out_qty);

    // new_cost = prev_cost - cost_reduction
    money_sub(prev_cost, *out_cost_reduction, out_cost);

    // realized_pnl = (sell_gross - sell_fee) - cost_reduction
    money_t sell_gross;
    price_times_quantity(sell_price, sell_qty, &sell_gross);
    money_t sell_net;
    money_sub(sell_gross, sell_fee, &sell_net);
    money_sub(sell_net, *out_cost_reduction, out_realized_pnl);

    return 0;
}

int
ledger_calc_unrealized_pnl(quantity_t qty,
                           price_t    net_val,
                           money_t    cost_basis,
                           money_t*   out_unrealized_pnl)
{
    if (!out_unrealized_pnl) {
        return -1;
    }
    money_t mkt_val;
    price_times_quantity(net_val, qty, &mkt_val);
    return money_sub(mkt_val, cost_basis, out_unrealized_pnl) == DECIMAL_OK ? 0 : -1;
}

/* =========================================================================
 * 2. 数据库事务操作算子 (Database Transaction Operators)
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

    char uid_str[32], ast_str[32], last_str[32], cat_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)tx->user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)tx->asset_id);
    snprintf(last_str, sizeof(last_str), "%lld", (long long)tx->linked_asset_id);
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)tx->category_id);

    char amt_buf[64], price_buf[64], qty_buf[64], fee_buf[64];
    money_to_string(tx->amount, amt_buf, sizeof(amt_buf));
    price_to_string(tx->price, price_buf, sizeof(price_buf));
    quantity_to_string_fixed(tx->quantity, 8, qty_buf, sizeof(qty_buf));
    money_to_string(tx->fee, fee_buf, sizeof(fee_buf));

    const char* cur_str = currency_code(&tx->amount.currency);
    const char* tx_type_str = tx->type_str ? tx->type_str : ledger_tx_type_to_str(tx->type);

    // 1. Insert parent transaction record if tx->id == 0
    if (tx->id <= 0) {
        const char* dir = "in";
        const char* ldir = (tx->linked_asset_id > 0) ? "out" : NULL;
        if (tx->type == LEDGER_TX_SELL || tx->type == LEDGER_TX_WITHDRAW ||
            tx->type == LEDGER_TX_TRANSFER_OUT || tx->type == LEDGER_TX_FEE ||
            tx->type == LEDGER_TX_TAX) {
            dir = "out";
            ldir = (tx->linked_asset_id > 0) ? "in" : NULL;
        }

        const char* parent_tx_str = NULL;
        char        ptx_buf[32];
        if (tx->parent_tx_id > 0) {
            snprintf(ptx_buf, sizeof(ptx_buf), "%lld", (long long)tx->parent_tx_id);
            parent_tx_str = ptx_buf;
        }

        const char* src_type =
            (tx->type == LEDGER_TX_DEPOSIT || tx->type == LEDGER_TX_TRANSFER_IN ||
             tx->type == LEDGER_TX_INTEREST || tx->type == LEDGER_TX_DIVIDEND)
                ? "income"
                : "expense";

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
                                    tx->tx_date ? tx->tx_date : "datetime('now')",
                                    tx->note ? tx->note : "",
                                    parent_tx_str,
                                    NULL};

        csilk_json_t* ins_res = csilk_db_query_param_json(
            pool,
            "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, "
            "source_type, transaction_type, direction, linked_direction, "
            "amount, price_per_unit, quantity, fee, currency, transaction_date, note, "
            "parent_tx_id) "
            "VALUES (?, ?, NULLIF(?, '0'), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
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
    }

    // 2. Position & Balance Updates
    bool    is_investment_tx = (tx->type == LEDGER_TX_BUY || tx->type == LEDGER_TX_SELL);
    money_t pos_delta = money_zero(tx->amount.currency);

    if (is_investment_tx) {
        int pos_rc = apply_position_fc(pool,
                                       tx->asset_id,
                                       tx_type_str,
                                       tx->amount,
                                       tx->fee,
                                       tx->price,
                                       tx->quantity,
                                       &pos_delta);
        if (pos_rc < 0) {
            return -1; // Position calculation / insufficient units error
        }
    }

    // 3. Fee record generation if fee > 0 on investment buy/sell
    if (!money_is_zero(tx->fee) && tx->linked_asset_id > 0 && is_investment_tx) {
        char tx_id_str[32];
        snprintf(tx_id_str, sizeof(tx_id_str), "%lld", (long long)tx->id);
        const char*   fee_note = (tx->note && tx->note[0]) ? tx->note : "fee";
        const char*   fee_params[] = {uid_str,
                                      ast_str,
                                      last_str,
                                      cat_str,
                                      "expense",
                                      "fee",
                                      "out",
                                      fee_buf,
                                      "0.0000",
                                      "0.0000",
                                      "0.0000",
                                      cur_str,
                                      tx->tx_date ? tx->tx_date : "datetime('now')",
                                      fee_note,
                                      tx_id_str,
                                      NULL};
        csilk_json_t* fee_res = csilk_db_query_param_json(
            pool,
            "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, "
            "source_type, transaction_type, direction, linked_direction, "
            "amount, price_per_unit, quantity, fee, currency, transaction_date, note, "
            "parent_tx_id) "
            "VALUES (?, ?, NULLIF(?, '0'), ?, ?, ?, ?, NULL, ?, ?, ?, ?, ?, ?, ?, ?)",
            fee_params);
        if (fee_res) {
            csilk_json_free(fee_res);
        }

        money_t neg_fee = money_neg(tx->fee);
        if (balance_apply_delta_m(pool,
                                  tx->linked_asset_id,
                                  tx->user_id,
                                  neg_fee,
                                  "transaction_fee",
                                  tx->id,
                                  fee_note) != 0) {
            return -1;
        }
    }

    // 4. Target Asset Balance Delta
    if (is_investment_tx) {
        if (!money_is_zero(pos_delta)) {
            if (balance_apply_delta_m(
                    pool, tx->asset_id, tx->user_id, pos_delta, "transaction", tx->id, tx->note) !=
                0) {
                return -1;
            }
        }
    } else {
        money_t tdelta = tx_delta_m(tx_type_str, tx->amount, tx->price, tx->quantity);
        if (!money_is_zero(tdelta)) {
            if (balance_apply_delta_m(
                    pool, tx->asset_id, tx->user_id, tdelta, "transaction", tx->id, tx->note) !=
                0) {
                return -1;
            }
        }
    }

    // 5. Linked Funding Asset Balance Delta
    if (tx->linked_asset_id > 0) {
        money_t ldelta;
        if (tx->type == LEDGER_TX_BUY) {
            ldelta = money_neg(tx->amount);
        } else if (tx->type == LEDGER_TX_SELL) {
            ldelta = tx->amount;
        } else {
            money_t tdelta = tx_delta_m(tx_type_str, tx->amount, tx->price, tx->quantity);
            ldelta = tx_effective_ldelta_m(tx_type_str, tx->amount, tdelta);
        }

        if (!money_is_zero(ldelta)) {
            if (balance_apply_delta_m(pool,
                                      tx->linked_asset_id,
                                      tx->user_id,
                                      ldelta,
                                      "transaction_linked",
                                      tx->id,
                                      tx->note) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

int
ledger_reverse_tx(csilk_db_pool_t* pool, int64_t user_id, int64_t tx_id)
{
    if (!pool || user_id <= 0 || tx_id <= 0) {
        return -1;
    }

    csilk_json_t* old_arr = tx_get_old(pool, user_id, tx_id);
    if (!old_arr || csilk_json_array_size(old_arr) == 0) {
        if (old_arr) {
            csilk_json_free(old_arr);
        }
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

    // 1. Query and reverse any fee children
    csilk_json_t* fee_children = tx_child_fee_rows(pool, user_id, tx_id);
    if (fee_children) {
        size_t fee_cnt = csilk_json_array_size(fee_children);
        for (size_t i = 0; i < fee_cnt; i++) {
            csilk_json_t* frow = csilk_json_array_get(fee_children, i);
            int64_t       f_last = (int64_t)db_get_int(frow, "linked_asset_id");
            money_t       f_amt = db_get_money(frow, "amount", cur);
            if (f_last > 0 && !money_is_zero(f_amt)) {
                // Reverse fee (fee originally debited -fee, so credit +fee)
                balance_apply_delta_m(pool,
                                      f_last,
                                      user_id,
                                      f_amt,
                                      "transaction_fee_reversal",
                                      tx_id,
                                      "fee rollback");
            }
        }
        csilk_json_free(fee_children);
        tx_delete_fee_children(pool, user_id, tx_id);
    }

    // 2. Position / Asset balance rollback
    bool is_inv = (type && (strcmp(type, "buy") == 0 || strcmp(type, "sell") == 0));
    if (is_inv) {
        money_t rollback_pos_delta = money_zero(cur);
        int     rb_rc = rollback_position_fc(
            pool, asset_id, type, amount, fee, price, qty, &rollback_pos_delta);
        if (rb_rc == 0 && !money_is_zero(rollback_pos_delta)) {
            balance_apply_delta_m(pool,
                                  asset_id,
                                  user_id,
                                  rollback_pos_delta,
                                  "transaction_reversal",
                                  tx_id,
                                  "position rollback");
        }
    } else {
        money_t tdelta = tx_delta_m(type, amount, price, qty);
        if (!money_is_zero(tdelta)) {
            money_t rev_tdelta = money_neg(tdelta);
            balance_apply_delta_m(
                pool, asset_id, user_id, rev_tdelta, "transaction_reversal", tx_id, "rollback");
        }
    }

    // 3. Linked funding asset delta rollback
    if (linked_asset_id > 0) {
        money_t ldelta;
        if (type && strcmp(type, "buy") == 0) {
            // buy originally debited -(amount + fee), but fee was rolled back above. Target amount rollback: +amount
            ldelta = amount;
        } else if (type && strcmp(type, "sell") == 0) {
            // sell originally credited +(amount - fee), target amount rollback: -amount
            ldelta = money_neg(amount);
        } else {
            money_t tdelta = tx_delta_m(type, amount, price, qty);
            money_t orig_ldelta = tx_effective_ldelta_m(type, amount, tdelta);
            ldelta = money_neg(orig_ldelta);
        }
        if (!money_is_zero(ldelta)) {
            balance_apply_delta_m(pool,
                                  linked_asset_id,
                                  user_id,
                                  ldelta,
                                  "transaction_linked_reversal",
                                  tx_id,
                                  "rollback");
        }
    }

    csilk_json_free(old_arr);
    return 0;
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
    money_t delta = is_income ? amount : money_neg(amount);
    return balance_apply_delta_m(pool, asset_id, user_id, delta, "daily_expense", expense_id, note);
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
    money_t delta = is_income ? money_neg(amount) : amount;
    return balance_apply_delta_m(
        pool, asset_id, user_id, delta, "daily_expense_reversal", expense_id, note);
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
    money_t neg_amount = money_neg(amount);
    if (balance_apply_delta_m(
            pool, from_asset_id, user_id, neg_amount, "transfer_out", transfer_id, note) != 0) {
        return -1;
    }
    if (balance_apply_delta_m(
            pool, to_asset_id, user_id, amount, "transfer_in", transfer_id, note) != 0) {
        return -1;
    }
    return 0;
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
    money_t neg_amount = money_neg(amount);
    if (balance_apply_delta_m(
            pool, from_asset_id, user_id, amount, "transfer_out_reversal", transfer_id, note) !=
        0) {
        return -1;
    }
    if (balance_apply_delta_m(
            pool, to_asset_id, user_id, neg_amount, "transfer_in_reversal", transfer_id, note) !=
        0) {
        return -1;
    }
    return 0;
}

/* =========================================================================
 * 3. 状态重算与事件溯源重建 (State Rebuild / Event Sourcing Engine)
 * ========================================================================= */

int
ledger_rebuild_position(csilk_db_pool_t*         pool,
                        int64_t                  user_id,
                        int64_t                  asset_id,
                        ledger_position_state_t* out_state)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return -1;
    }

    char uid_str[32], ast_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);

    // 1. Fetch asset metadata & currency
    csilk_json_t* a_rows =
        csilk_db_query_param_json(pool,
                                  "SELECT currency FROM assets WHERE id=? AND user_id=?",
                                  (const char*[]){ast_str, uid_str, NULL});
    if (!a_rows || csilk_json_array_size(a_rows) == 0) {
        if (a_rows) {
            csilk_json_free(a_rows);
        }
        return -1;
    }
    const char* cur_str = csilk_json_get_string(csilk_json_array_get(a_rows, 0), "currency");
    currency_t  cur = currency_from_str(cur_str);
    csilk_json_free(a_rows);

    // 2. Query all investment transactions in chronological order
    csilk_json_t* tx_rows = csilk_db_query_param_json(
        pool,
        "SELECT id, transaction_type, amount, price_per_unit, quantity, fee "
        "FROM transactions WHERE user_id=? AND asset_id=? AND transaction_type IN ('buy', 'sell') "
        "ORDER BY transaction_date ASC, id ASC",
        (const char*[]){uid_str, ast_str, NULL});

    quantity_t curr_qty = quantity_zero();
    money_t    curr_cost = money_zero(cur);
    price_t    latest_price = price_zero(cur);
    money_t    cum_realized = money_zero(cur);

    if (tx_rows) {
        size_t n = csilk_json_array_size(tx_rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(tx_rows, i);
            const char*   type = csilk_json_get_string(r, "transaction_type");
            price_t       p = db_get_price(r, "price_per_unit", cur);
            quantity_t    q = db_get_quantity(r, "quantity");
            money_t       f = db_get_money(r, "fee", cur);

            if (type && strcmp(type, "buy") == 0) {
                ledger_calc_buy_position(curr_qty, curr_cost, q, p, f, &curr_qty, &curr_cost);
                if (!decimal_is_zero(p.unit_price)) {
                    latest_price = p;
                }
            } else if (type && strcmp(type, "sell") == 0) {
                money_t cost_red, real_pnl;
                ledger_calc_sell_position(
                    curr_qty, curr_cost, q, p, f, &curr_qty, &curr_cost, &cost_red, &real_pnl);
                money_add(cum_realized, real_pnl, &cum_realized);
            }
        }
        csilk_json_free(tx_rows);
    }

    money_t curr_mkt_val;
    price_times_quantity(latest_price, curr_qty, &curr_mkt_val);

    money_t curr_unrealized;
    ledger_calc_unrealized_pnl(curr_qty, latest_price, curr_cost, &curr_unrealized);

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
    if (upd_res) {
        csilk_json_free(upd_res);
    }

    if (out_state) {
        out_state->asset_id = asset_id;
        out_state->quantity = curr_qty;
        out_state->cost_basis = curr_cost;
        out_state->net_value = latest_price;
        out_state->current_value = curr_mkt_val;
        out_state->realized_pnl = cum_realized;
        out_state->unrealized_pnl = curr_unrealized;
    }
    return 0;
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

    char uid_str[32], ast_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);

    // 1. Fetch asset currency & category liability flag
    csilk_json_t* a_rows =
        csilk_db_query_param_json(pool,
                                  "SELECT a.currency, c.asset_type FROM assets a "
                                  "JOIN categories c ON a.category_id=c.id "
                                  "WHERE a.id=? AND a.user_id=?",
                                  (const char*[]){ast_str, uid_str, NULL});

    if (!a_rows || csilk_json_array_size(a_rows) == 0) {
        if (a_rows) {
            csilk_json_free(a_rows);
        }
        return -1;
    }
    csilk_json_t* a_obj = csilk_json_array_get(a_rows, 0);
    const char*   cur_str = csilk_json_get_string(a_obj, "currency");
    currency_t    cur = currency_from_str(cur_str);
    csilk_json_free(a_rows);

    // 2. Replay all balance audit logs in chronological order
    csilk_json_t* log_rows = csilk_db_query_param_json(
        pool,
        "SELECT delta FROM asset_balance_logs WHERE asset_id=? ORDER BY created_at ASC, id ASC",
        (const char*[]){ast_str, NULL});

    money_t cum_bal = money_zero(cur);
    int64_t tx_count = 0;

    if (log_rows) {
        size_t n = csilk_json_array_size(log_rows);
        tx_count = (int64_t)n;
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(log_rows, i);
            money_t       d = db_get_money(r, "delta", cur);
            money_add(cum_bal, d, &cum_bal);
        }
        csilk_json_free(log_rows);
    }

    // 3. Update assets current_value
    char bal_str[64];
    money_to_string(cum_bal, bal_str, sizeof(bal_str));
    const char*   upd_params[] = {bal_str, ast_str, uid_str, NULL};
    csilk_json_t* upd_res = csilk_db_query_param_json(
        pool,
        "UPDATE assets SET current_value=?, updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=?",
        upd_params);
    if (upd_res) {
        csilk_json_free(upd_res);
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

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* assets = csilk_db_query_param_json(pool,
                                                     "SELECT a.id, c.asset_type FROM assets a "
                                                     "JOIN categories c ON a.category_id=c.id "
                                                     "WHERE a.user_id=?",
                                                     (const char*[]){uid_str, NULL});

    if (!assets) {
        return 0;
    }

    size_t n = csilk_json_array_size(assets);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(assets, i);
        int64_t       aid = db_get_int(r, "id");
        const char*   atype = csilk_json_get_string(r, "asset_type");
        bool is_inv = (atype && (strcmp(atype, "stock") == 0 || strcmp(atype, "fund") == 0 ||
                                 strcmp(atype, "bond") == 0 || strcmp(atype, "crypto") == 0));
        if (is_inv) {
            ledger_rebuild_position(pool, user_id, aid, NULL);
        } else {
            ledger_rebuild_account(pool, user_id, aid, NULL);
        }
    }
    csilk_json_free(assets);
    return 0;
}
