#include "domain/asset/cost_basis.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static inline quantity_t q_make(double d) {
    quantity_t q;
    quantity_from_double(d, 4, &q);
    return q;
}

static inline money_t m_make(double d, currency_t cur) {
    money_t m;
    money_from_double(d, cur, &m);
    return m;
}

void test_cost_basis_buy_and_average() {
    mf_cost_basis_t cb = mf_cost_basis_init(CURRENCY_CNY);

    // Buy 100 shares @ 10 CNY, fee 5 CNY -> total cost 1005 CNY, avg cost 10.05
    assert(mf_cost_basis_apply_buy(&cb, q_make(100), m_make(1000, CURRENCY_CNY), m_make(5, CURRENCY_CNY)) == 0);

    assert(quantity_to_double(cb.quantity) == 100.0);
    assert(money_to_double(cb.total_cost) == 1005.0);
    assert(fabs(price_to_double(cb.average_cost) - 10.05) < 1e-4);

    // Buy another 100 shares @ 20 CNY, fee 5 CNY -> total cost 1005 + 2005 = 3010 CNY, avg cost 3010/200 = 15.05
    assert(mf_cost_basis_apply_buy(&cb, q_make(100), m_make(2000, CURRENCY_CNY), m_make(5, CURRENCY_CNY)) == 0);

    assert(quantity_to_double(cb.quantity) == 200.0);
    assert(money_to_double(cb.total_cost) == 3010.0);
    assert(fabs(price_to_double(cb.average_cost) - 15.05) < 1e-4);
    printf("PASS: test_cost_basis_buy_and_average\n");
}

void test_cost_basis_sell_proportional() {
    mf_cost_basis_t cb = mf_cost_basis_init(CURRENCY_CNY);
    money_t realized = money_zero(CURRENCY_CNY);

    // Buy 200 shares @ 10 CNY (cost 2000)
    assert(mf_cost_basis_apply_buy(&cb, q_make(200), m_make(2000, CURRENCY_CNY), money_zero(CURRENCY_CNY)) == 0);

    // Sell 50 shares @ 15 CNY (amount 750, fee 10) -> proceeds 740, cost deducted 500, realized = 740 - 500 = 240
    assert(mf_cost_basis_apply_sell(&cb, &realized, q_make(50), m_make(750, CURRENCY_CNY), m_make(10, CURRENCY_CNY)) == 0);

    assert(quantity_to_double(cb.quantity) == 150.0);
    assert(money_to_double(cb.total_cost) == 1500.0);
    assert(money_to_double(realized) == 240.0);

    // Oversell defense: sell 200 shares when only 150 available -> must return error -1
    assert(mf_cost_basis_apply_sell(&cb, &realized, q_make(200), m_make(3000, CURRENCY_CNY), money_zero(CURRENCY_CNY)) == -1);

    // Sell remaining 150 shares -> total cost should reset to 0
    assert(mf_cost_basis_apply_sell(&cb, &realized, q_make(150), m_make(1500, CURRENCY_CNY), money_zero(CURRENCY_CNY)) == 0);
    assert(quantity_to_double(cb.quantity) == 0.0);
    assert(money_to_double(cb.total_cost) == 0.0);
    assert(price_to_double(cb.average_cost) == 0.0);

    printf("PASS: test_cost_basis_sell_proportional\n");
}

void test_cost_basis_dividend() {
    mf_cost_basis_t cb = mf_cost_basis_init(CURRENCY_CNY);
    money_t realized = money_zero(CURRENCY_CNY);

    assert(mf_cost_basis_apply_buy(&cb, q_make(100), m_make(1000, CURRENCY_CNY), money_zero(CURRENCY_CNY)) == 0);

    // Dividend 100 CNY -> realized +100, total_cost_pnl reduced to 900
    assert(mf_cost_basis_apply_dividend(&cb, &realized, m_make(100, CURRENCY_CNY)) == 0);
    assert(money_to_double(realized) == 100.0);
    assert(money_to_double(cb.total_cost_pnl) == 900.0);
    // Holding quantity & accounting total_cost remain unchanged
    assert(quantity_to_double(cb.quantity) == 100.0);
    assert(money_to_double(cb.total_cost) == 1000.0);

    printf("PASS: test_cost_basis_dividend\n");
}

int main() {
    test_cost_basis_buy_and_average();
    test_cost_basis_sell_proportional();
    test_cost_basis_dividend();
    printf("All domain cost basis tests passed!\n");
    return 0;
}
