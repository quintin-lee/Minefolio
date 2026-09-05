#include "domain/asset/pnl.h"
#include "core/financial/percentage.h"
#include <string.h>

mf_pnl_t
mf_pnl_calculate(money_t cost_basis, money_t market_value, money_t realized_pnl)
{
    mf_pnl_t pnl;
    memset(&pnl, 0, sizeof(pnl));
    pnl.realized_pnl = realized_pnl;

    // unrealized_pnl = market_value - cost_basis
    money_t un_pnl;
    if (money_sub(market_value, cost_basis, &un_pnl) == DECIMAL_OK) {
        pnl.unrealized_pnl = un_pnl;
    } else {
        pnl.unrealized_pnl = money_zero(cost_basis.currency);
    }

    // total_pnl = realized_pnl + unrealized_pnl
    money_t tot_pnl;
    if (money_add(pnl.realized_pnl, pnl.unrealized_pnl, &tot_pnl) == DECIMAL_OK) {
        pnl.total_pnl = tot_pnl;
    } else {
        pnl.total_pnl = money_zero(cost_basis.currency);
    }

    percentage_t un_pct, tot_pct;
    if (percentage_calc(pnl.unrealized_pnl, cost_basis, 4, ROUND_HALF_UP, &un_pct) == DECIMAL_OK) {
        pnl.unrealized_pct = percentage_to_double(un_pct);
    } else {
        pnl.unrealized_pct = 0.0;
    }

    if (percentage_calc(pnl.total_pnl, cost_basis, 4, ROUND_HALF_UP, &tot_pct) == DECIMAL_OK) {
        pnl.total_return_pct = percentage_to_double(tot_pct);
    } else {
        pnl.total_return_pct = 0.0;
    }

    return pnl;
}
