#include "domain/asset/pnl.h"
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

    double cost_d = money_to_double(cost_basis);
    double un_d = money_to_double(pnl.unrealized_pnl);
    double tot_d = money_to_double(pnl.total_pnl);

    pnl.unrealized_pct = (cost_d != 0.0) ? (un_d / cost_d) * 100.0 : 0.0;
    pnl.total_return_pct = (cost_d != 0.0) ? (tot_d / cost_d) * 100.0 : 0.0;

    return pnl;
}
