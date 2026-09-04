#include "domain/asset/cost_basis.h"
#include <string.h>

mf_cost_basis_t mf_cost_basis_init(currency_t currency) {
    mf_cost_basis_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.currency = currency;
    cb.quantity = quantity_zero();
    cb.total_cost = money_zero(currency);
    cb.total_cost_pnl = money_zero(currency);
    cb.average_cost = price_zero(currency);
    return cb;
}

int mf_cost_basis_apply_buy(mf_cost_basis_t* cb,
                            quantity_t       qty,
                            money_t          amount,
                            money_t          fee) {
    if (!cb) return -1;
    if (quantity_is_negative(qty) || quantity_is_zero(qty)) return -1;

    // cb->quantity += qty
    quantity_t new_qty;
    if (quantity_add(cb->quantity, qty, &new_qty) != DECIMAL_OK) return -1;

    // total_cost += amount + fee
    money_t cost_add;
    if (money_add(amount, fee, &cost_add) != DECIMAL_OK) return -1;
    money_t new_total_cost;
    if (money_add(cb->total_cost, cost_add, &new_total_cost) != DECIMAL_OK) return -1;

    // total_cost_pnl += amount (excludes fee for pnl baseline calculation)
    money_t new_total_cost_pnl;
    if (money_add(cb->total_cost_pnl, amount, &new_total_cost_pnl) != DECIMAL_OK) return -1;

    cb->quantity = new_qty;
    cb->total_cost = new_total_cost;
    cb->total_cost_pnl = new_total_cost_pnl;

    // average_cost = total_cost / quantity
    price_t avg_p;
    if (money_div_quantity(cb->total_cost, cb->quantity, 4, ROUND_HALF_UP, &avg_p) == DECIMAL_OK) {
        cb->average_cost = avg_p;
    } else {
        cb->average_cost = price_zero(cb->currency);
    }
    return 0;
}

int mf_cost_basis_apply_sell(mf_cost_basis_t* cb,
                             money_t*         accum_realized_pnl,
                             quantity_t       qty,
                             money_t          amount,
                             money_t          fee) {
    if (!cb) return -1;
    if (quantity_is_negative(qty) || quantity_is_zero(qty)) return -1;

    // Oversell check: qty > cb->quantity
    if (quantity_cmp(qty, cb->quantity) > 0) {
        return -1;
    }

    double cur_qty_d = quantity_to_double(cb->quantity);
    double sell_qty_d = quantity_to_double(qty);
    if (cur_qty_d <= 0.0) return -1;

    double ratio = sell_qty_d / cur_qty_d;
    double cur_total_cost_d = money_to_double(cb->total_cost);
    double cur_pnl_cost_d = money_to_double(cb->total_cost_pnl);

    double cost_deducted = cur_total_cost_d * ratio;
    double pnl_cost_deducted = cur_pnl_cost_d * ratio;

    // Net sell proceeds = amount - fee
    double proceeds_d = money_to_double(amount) - money_to_double(fee);
    double trade_realized_d = proceeds_d - pnl_cost_deducted;

    // Update accumulated realized pnl
    if (accum_realized_pnl) {
        double old_realized = money_to_double(*accum_realized_pnl);
        money_from_double(old_realized + trade_realized_d, cb->currency, accum_realized_pnl);
    }

    // Deduct quantity
    quantity_t new_qty;
    quantity_sub(cb->quantity, qty, &new_qty);
    cb->quantity = new_qty;

    if (quantity_is_zero(cb->quantity)) {
        cb->total_cost = money_zero(cb->currency);
        cb->total_cost_pnl = money_zero(cb->currency);
        cb->average_cost = price_zero(cb->currency);
    } else {
        money_from_double(cur_total_cost_d - cost_deducted, cb->currency, &cb->total_cost);
        money_from_double(cur_pnl_cost_d - pnl_cost_deducted, cb->currency, &cb->total_cost_pnl);
        price_t avg_p;
        if (money_div_quantity(cb->total_cost, cb->quantity, 4, ROUND_HALF_UP, &avg_p) == DECIMAL_OK) {
            cb->average_cost = avg_p;
        }
    }

    return 0;
}

int mf_cost_basis_apply_dividend(mf_cost_basis_t* cb,
                                 money_t*         accum_realized_pnl,
                                 money_t          dividend_amount) {
    if (!cb) return -1;
    if (money_is_negative(dividend_amount) || money_is_zero(dividend_amount)) return -1;

    if (accum_realized_pnl) {
        money_t new_realized;
        if (money_add(*accum_realized_pnl, dividend_amount, &new_realized) == DECIMAL_OK) {
            *accum_realized_pnl = new_realized;
        }
    }

    // Dividend reduces total_cost_pnl baseline
    money_t new_pnl_cost;
    if (money_sub(cb->total_cost_pnl, dividend_amount, &new_pnl_cost) == DECIMAL_OK) {
        cb->total_cost_pnl = new_pnl_cost;
    }
    return 0;
}
