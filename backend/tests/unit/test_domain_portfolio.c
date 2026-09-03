#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "domain/portfolio/entity.h"
#include "domain/portfolio/rules.h"
#include "core/financial/currency.h"

static void test_portfolio_trade_events_and_pnl(void) {
    currency_t cny = currency_from_str("CNY");

    mf_holding_item_t item = {0};
    item.asset_id = 100;
    snprintf(item.name, sizeof(item.name), "招商中证白酒");
    snprintf(item.asset_type, sizeof(item.asset_type), "fund");
    item.currency = cny;

    quantity_from_double(600.0, 4, &item.quantity);
    price_from_double(2.50, 4, cny, &item.net_value);
    money_from_double(1200.0, cny, &item.cost_basis);

    mf_portfolio_trade_event_t events[3] = {0};

    /* 1. Buy 1000 @ 2.0 = 2000 */
    events[0].asset_id = 100;
    snprintf(events[0].type, sizeof(events[0].type), "buy");
    quantity_from_double(1000.0, 4, &events[0].quantity);
    price_from_double(2.0, 4, cny, &events[0].price);
    money_from_double(2000.0, cny, &events[0].amount);

    /* 2. Sell 400 @ 3.0 = 1200 (realized = 1200 - 400*2 = 400) */
    events[1].asset_id = 100;
    snprintf(events[1].type, sizeof(events[1].type), "sell");
    quantity_from_double(400.0, 4, &events[1].quantity);
    price_from_double(3.0, 4, cny, &events[1].price);
    money_from_double(1200.0, cny, &events[1].amount);

    /* 3. Dividend income 100 */
    events[2].asset_id = 100;
    snprintf(events[2].type, sizeof(events[2].type), "income");
    quantity_from_double(0.0, 4, &events[2].quantity);
    money_from_double(100.0, cny, &events[2].amount);

    int rc = mf_portfolio_rule_apply_trade_events(&item, 1, events, 3);
    assert(rc == 0);

    /* 市场价值 = 600 * 2.50 = 1500 */
    assert(money_to_double(item.market_value) == 1500.0);
    /* 浮动盈亏 = 1500 - 1200 = 300 */
    assert(money_to_double(item.floating_pnl) == 300.0);
    assert(item.floating_pct == 25.0);
    /* 已实现盈亏 = 400 + 100 = 500 */
    assert(money_to_double(item.realized_pnl) == 500.0);

    printf("PASS: test_portfolio_trade_events_and_pnl\n");
}

static void test_portfolio_summary_aggregation(void) {
    currency_t cny = currency_from_str("CNY");

    mf_holding_item_t items[2] = {0};
    items[0].asset_id = 1;
    money_from_double(1500.0, cny, &items[0].market_value);
    money_from_double(1200.0, cny, &items[0].cost_basis);
    money_from_double(300.0, cny, &items[0].floating_pnl);
    money_from_double(500.0, cny, &items[0].realized_pnl);

    items[1].asset_id = 2;
    money_from_double(2500.0, cny, &items[1].market_value);
    money_from_double(2000.0, cny, &items[1].cost_basis);
    money_from_double(500.0, cny, &items[1].floating_pnl);
    money_from_double(100.0, cny, &items[1].realized_pnl);

    mf_portfolio_summary_t sum = {0};
    int rc = mf_portfolio_rule_aggregate_summary(items, 2, cny, &sum);
    assert(rc == 0);

    assert(money_to_double(sum.total_market_value) == 4000.0);
    assert(money_to_double(sum.total_cost_basis) == 3200.0);
    assert(money_to_double(sum.total_floating_pnl) == 800.0);
    assert(money_to_double(sum.total_realized_pnl) == 600.0);
    assert(sum.floating_pct == 25.0);

    printf("PASS: test_portfolio_summary_aggregation\n");
}

int main(void) {
    test_portfolio_trade_events_and_pnl();
    test_portfolio_summary_aggregation();
    printf("All domain portfolio tests passed successfully!\n");
    return 0;
}
