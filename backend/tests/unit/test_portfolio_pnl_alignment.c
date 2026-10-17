#include "domain/portfolio/rules.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

int main(void) {
    currency_t cny = currency_from_str("CNY");

    // Scenario: Buy 1000 @ 2.0 = 2000, fee 10; Sell 400 @ 3.0 = 1200, fee 8; Dividend 100
    mf_holding_item_t item = {0};
    item.asset_id = 200;
    item.currency = cny;
    quantity_from_double(600.0, 4, &item.quantity);
    price_from_double(2.5, 4, cny, &item.net_value);
    money_from_double(1200.0, cny, &item.cost_basis);

    mf_portfolio_trade_event_t events[3] = {0};
    // 1. Buy 1000 @ 2.0 = 2000, fee 10
    events[0].asset_id = 200;
    snprintf(events[0].type, sizeof(events[0].type), "buy");
    quantity_from_double(1000.0, 4, &events[0].quantity);
    price_from_double(2.0, 4, cny, &events[0].price);
    money_from_double(2000.0, cny, &events[0].amount);
    money_from_double(10.0, cny, &events[0].fee);

    // 2. Sell 400 @ 3.0 = 1200, fee 8
    events[1].asset_id = 200;
    snprintf(events[1].type, sizeof(events[1].type), "sell");
    quantity_from_double(400.0, 4, &events[1].quantity);
    price_from_double(3.0, 4, cny, &events[1].price);
    money_from_double(1200.0, cny, &events[1].amount);
    money_from_double(8.0, cny, &events[1].fee);

    // 3. Dividend 100
    events[2].asset_id = 200;
    snprintf(events[2].type, sizeof(events[2].type), "dividend");
    money_from_double(100.0, cny, &events[2].amount);

    int rc = mf_portfolio_rule_apply_trade_events(&item, 1, events, 3);
    assert(rc == 0);

    // Realized capital gain = (1200 - 8) - 2010 * 0.4 + 100 = 488.0
    double realized = money_to_double(item.realized_pnl);
    assert(fabs(realized - 488.0) < 1e-4);

    // Cost basis remaining for 600 shares = 1200.0
    double cost = money_to_double(item.cost_basis);
    assert(fabs(cost - 1200.0) < 1e-4);

    printf("PASS: test_portfolio_pnl_alignment\n");
    return 0;
}
