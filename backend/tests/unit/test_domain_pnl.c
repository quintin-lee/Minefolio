#include "domain/asset/pnl.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static inline money_t m_make(double d, currency_t cur) {
    money_t m;
    money_from_double(d, cur, &m);
    return m;
}

void test_pnl_unrealized_and_total() {
    money_t cost = m_make(1000, CURRENCY_CNY);
    money_t market = m_make(1500, CURRENCY_CNY);
    money_t realized = m_make(200, CURRENCY_CNY);

    mf_pnl_t pnl = mf_pnl_calculate(cost, market, realized);
    assert(money_to_double(pnl.unrealized_pnl) == 500.0);
    assert(money_to_double(pnl.realized_pnl) == 200.0);
    assert(money_to_double(pnl.total_pnl) == 700.0);
    assert(fabs(pnl.unrealized_pct - 50.0) < 1e-4);
    assert(fabs(pnl.total_return_pct - 70.0) < 1e-4);

    printf("PASS: test_pnl_unrealized_and_total\n");
}

void test_pnl_closed_position_zero_cost() {
    /* DEF-08: Position completely closed: cost_basis=0, market=0, realized=500 */
    money_t cost = m_make(0, CURRENCY_CNY);
    money_t market = m_make(0, CURRENCY_CNY);
    money_t realized = m_make(500, CURRENCY_CNY);

    mf_pnl_t pnl = mf_pnl_calculate(cost, market, realized);
    assert(money_to_double(pnl.unrealized_pnl) == 0.0);
    assert(money_to_double(pnl.realized_pnl) == 500.0);
    assert(money_to_double(pnl.total_pnl) == 500.0);
    assert(pnl.unrealized_pct == 0.0);
    assert(pnl.total_return_pct == 0.0);
    printf("PASS: test_pnl_closed_position_zero_cost\n");
}

int main() {
    test_pnl_unrealized_and_total();
    test_pnl_closed_position_zero_cost();
    printf("All domain pnl tests passed!\n");
    return 0;
}
