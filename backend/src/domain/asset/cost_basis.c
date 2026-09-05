#include "domain/asset/cost_basis.h"
#include <string.h>

mf_cost_basis_t
mf_cost_basis_init(currency_t currency)
{
    mf_cost_basis_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.currency = currency;
    cb.quantity = quantity_zero();
    cb.total_cost = money_zero(currency);
    cb.total_cost_pnl = money_zero(currency);
    cb.average_cost = price_zero(currency);
    return cb;
}

int
mf_cost_basis_apply_buy(mf_cost_basis_t* cb, quantity_t qty, money_t amount, money_t fee)
{
    if (!cb) {
        return -1;
    }
    if (quantity_is_negative(qty) || quantity_is_zero(qty)) {
        return -1;
    }

    // cb->quantity += qty
    quantity_t new_qty;
    if (quantity_add(cb->quantity, qty, &new_qty) != DECIMAL_OK) {
        return -1;
    }

    // total_cost += amount + fee
    money_t cost_add;
    if (money_add(amount, fee, &cost_add) != DECIMAL_OK) {
        return -1;
    }
    money_t new_total_cost;
    if (money_add(cb->total_cost, cost_add, &new_total_cost) != DECIMAL_OK) {
        return -1;
    }

    // total_cost_pnl += amount (excludes fee for pnl baseline calculation)
    money_t new_total_cost_pnl;
    if (money_add(cb->total_cost_pnl, amount, &new_total_cost_pnl) != DECIMAL_OK) {
        return -1;
    }

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

int
mf_cost_basis_apply_sell(
    mf_cost_basis_t* cb, money_t* accum_realized_pnl, quantity_t qty, money_t amount, money_t fee)
{
    if (!cb) {
        return -1;
    }
    if (quantity_is_negative(qty) || quantity_is_zero(qty)) {
        return -1;
    }

    // Oversell check: qty > cb->quantity
    if (quantity_cmp(qty, cb->quantity) > 0) {
        return -1;
    }

    // Net sell proceeds = amount - fee
    money_t proceeds;
    if (money_sub(amount, fee, &proceeds) != DECIMAL_OK) {
        return -1;
    }

    // Full sell check: qty == cb->quantity
    if (quantity_cmp(qty, cb->quantity) == 0) {
        money_t pnl_cost_deducted = cb->total_cost_pnl;
        money_t trade_realized;
        money_sub(proceeds, pnl_cost_deducted, &trade_realized);

        if (accum_realized_pnl) {
            money_add(*accum_realized_pnl, trade_realized, accum_realized_pnl);
        }

        cb->quantity = quantity_zero();
        cb->total_cost = money_zero(cb->currency);
        cb->total_cost_pnl = money_zero(cb->currency);
        cb->average_cost = price_zero(cb->currency);
        return 0;
    }

    // Partial sell:
    // ratio = qty / cb->quantity
    decimal_t ratio;
    if (decimal_div(qty.units, cb->quantity.units, 12, ROUND_HALF_UP, &ratio) != DECIMAL_OK) {
        return -1;
    }

    // cost_deducted = cb->total_cost * ratio
    decimal_t red_dec;
    if (decimal_mul(cb->total_cost.amount, ratio, &red_dec) != DECIMAL_OK) {
        return -1;
    }
    uint8_t prec = currency_precision(cb->currency);
    money_t cost_deducted;
    cost_deducted.currency = cb->currency;
    decimal_round(red_dec, (int32_t)prec, ROUND_HALF_UP, &cost_deducted.amount);

    // pnl_cost_deducted = cb->total_cost_pnl * ratio
    decimal_t pnl_red_dec;
    if (decimal_mul(cb->total_cost_pnl.amount, ratio, &pnl_red_dec) != DECIMAL_OK) {
        return -1;
    }
    money_t pnl_cost_deducted;
    pnl_cost_deducted.currency = cb->currency;
    decimal_round(pnl_red_dec, (int32_t)prec, ROUND_HALF_UP, &pnl_cost_deducted.amount);

    // trade_realized = proceeds - pnl_cost_deducted
    money_t trade_realized;
    money_sub(proceeds, pnl_cost_deducted, &trade_realized);

    if (accum_realized_pnl) {
        money_add(*accum_realized_pnl, trade_realized, accum_realized_pnl);
    }

    // Deduct quantity
    quantity_sub(cb->quantity, qty, &cb->quantity);

    // Deduct costs
    money_sub(cb->total_cost, cost_deducted, &cb->total_cost);
    money_sub(cb->total_cost_pnl, pnl_cost_deducted, &cb->total_cost_pnl);

    // Recompute average cost
    price_t avg_p;
    if (money_div_quantity(cb->total_cost, cb->quantity, 4, ROUND_HALF_UP, &avg_p) == DECIMAL_OK) {
        cb->average_cost = avg_p;
    } else {
        cb->average_cost = price_zero(cb->currency);
    }

    return 0;
}

int
mf_cost_basis_apply_dividend(mf_cost_basis_t* cb,
                             money_t*         accum_realized_pnl,
                             money_t          dividend_amount)
{
    if (!cb) {
        return -1;
    }
    if (money_is_negative(dividend_amount) || money_is_zero(dividend_amount)) {
        return -1;
    }

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
