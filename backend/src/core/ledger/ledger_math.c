#include "core/ledger/ledger_internal.h"
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
    if (strcasecmp(str, "withdraw") == 0 || strcasecmp(str, "withdrawal") == 0 ||
        strcasecmp(str, "expense") == 0) {
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
 * 纯数学计算算子 (Pure Mathematical Operators)
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
ledger_calc_sell_position_fifo(csilk_db_pool_t* pool,
                               int64_t          user_id,
                               int64_t          asset_id,
                               quantity_t       sell_qty,
                               price_t          sell_price,
                               money_t          sell_fee,
                               quantity_t*      out_qty,
                               money_t*         out_cost,
                               money_t*         out_cost_reduction,
                               money_t*         out_realized_pnl)
{
    if (!pool || user_id <= 0 || asset_id <= 0 || !out_qty || !out_cost || !out_cost_reduction ||
        !out_realized_pnl) {
        return -1;
    }
    if (quantity_is_negative(sell_qty) || quantity_is_zero(sell_qty)) {
        return -1;
    }

    csilk_json_t* lots = tx_lots_get_open(pool, user_id, asset_id);
    if (!lots || csilk_json_array_size(lots) == 0) {
        if (lots) {
            csilk_json_free(lots);
        }
        return -1; // Insufficient shares (no open lots)
    }

    currency_t cur = sell_price.currency;
    size_t     lot_count = csilk_json_array_size(lots);

    // 1. Check total available quantity across all open lots and initial total cost
    quantity_t total_avail_qty = quantity_zero();
    money_t    total_avail_cost = money_zero(cur);

    for (size_t i = 0; i < lot_count; i++) {
        const csilk_json_t* lot_obj = csilk_json_array_get(lots, i);
        quantity_t          rem_q = db_get_quantity(lot_obj, "remaining_quantity");
        price_t             cpu = db_get_price(lot_obj, "cost_per_unit", cur);
        quantity_add(total_avail_qty, rem_q, &total_avail_qty);

        money_t lot_cost;
        price_times_quantity(cpu, rem_q, &lot_cost);
        money_add(total_avail_cost, lot_cost, &total_avail_cost);
    }

    if (quantity_cmp(sell_qty, total_avail_qty) > 0) {
        csilk_json_free(lots);
        return -1; // Insufficient shares
    }

    // 2. Multi-lot deduction loop
    quantity_t sell_remaining = sell_qty;
    money_t    total_cost_reduction = money_zero(cur);

    for (size_t i = 0; i < lot_count; i++) {
        if (quantity_is_zero(sell_remaining)) {
            break;
        }

        const csilk_json_t* lot_obj = csilk_json_array_get(lots, i);
        int64_t             lot_id = db_get_int(lot_obj, "id");
        quantity_t          lot_rem = db_get_quantity(lot_obj, "remaining_quantity");
        price_t             cpu = db_get_price(lot_obj, "cost_per_unit", cur);

        quantity_t deduct_qty;
        if (quantity_cmp(lot_rem, sell_remaining) <= 0) {
            deduct_qty = lot_rem;
        } else {
            deduct_qty = sell_remaining;
        }

        money_t lot_cost_red;
        price_times_quantity(cpu, deduct_qty, &lot_cost_red);
        money_add(total_cost_reduction, lot_cost_red, &total_cost_reduction);

        quantity_t new_rem;
        quantity_sub(lot_rem, deduct_qty, &new_rem);
        bool is_closed = quantity_is_zero(new_rem);

        if (tx_lot_deduct(pool, user_id, lot_id, deduct_qty, new_rem, is_closed) != 0) {
            csilk_json_free(lots);
            return -1;
        }

        quantity_sub(sell_remaining, deduct_qty, &sell_remaining);
    }

    csilk_json_free(lots);

    // 3. Compute final position state & realized PnL
    quantity_sub(total_avail_qty, sell_qty, out_qty);
    if (quantity_is_zero(*out_qty)) {
        *out_cost = money_zero(cur);
    } else {
        money_sub(total_avail_cost, total_cost_reduction, out_cost);
        if (money_is_negative(*out_cost)) {
            *out_cost = money_zero(cur);
        }
    }
    *out_cost_reduction = total_cost_reduction;

    // Gross proceeds = sell_price * sell_qty
    money_t sell_gross;
    price_times_quantity(sell_price, sell_qty, &sell_gross);

    // Net proceeds = sell_gross - sell_fee
    money_t sell_net;
    money_sub(sell_gross, sell_fee, &sell_net);

    // Realized PnL = sell_net - total_cost_reduction
    money_sub(sell_net, total_cost_reduction, out_realized_pnl);

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
