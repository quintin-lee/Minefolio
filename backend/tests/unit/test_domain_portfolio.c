#include "domain/portfolio/portfolio.h"
#include "domain/portfolio/entity.h"
#include "domain/portfolio/rules.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static inline money_t m_make(double d, currency_t cur) {
    money_t m;
    money_from_double(d, cur, &m);
    return m;
}

static inline rate_t r_make(double factor, currency_t from, currency_t to) {
    rate_t r;
    rate_from_double(factor, 4, from, to, &r);
    return r;
}

/* 1. 历史兼容测试：交易事件重放与盈亏推导 */
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

/* 1b. 关键回归测试：连续分批卖出时成本池必须递减，禁止已实现盈亏爆炸性失真 */
static void test_portfolio_trade_events_partial_sells_cost_pool_decrement(void) {
    currency_t cny = currency_from_str("CNY");

    mf_holding_item_t item = {0};
    item.asset_id = 101;
    snprintf(item.name, sizeof(item.name), "测试分批卖出标的");
    item.currency = cny;
    quantity_from_double(0.0, 4, &item.quantity);
    price_from_double(12.0, 4, cny, &item.net_value);
    money_from_double(0.0, cny, &item.cost_basis);

    mf_portfolio_trade_event_t events[3] = {0};

    /* 1. Buy 100 @ 10 = 1000 */
    events[0].asset_id = 101;
    snprintf(events[0].type, sizeof(events[0].type), "buy");
    quantity_from_double(100.0, 4, &events[0].quantity);
    price_from_double(10.0, 4, cny, &events[0].price);
    money_from_double(1000.0, cny, &events[0].amount);

    /* 2. Sell 50 @ 12 = 600 (realized = 600 - 500 = +100) */
    events[1].asset_id = 101;
    snprintf(events[1].type, sizeof(events[1].type), "sell");
    quantity_from_double(50.0, 4, &events[1].quantity);
    price_from_double(12.0, 4, cny, &events[1].price);
    money_from_double(600.0, cny, &events[1].amount);

    /* 3. Sell 50 @ 12 = 600 (realized = 600 - 500 = +100) */
    events[2].asset_id = 101;
    snprintf(events[2].type, sizeof(events[2].type), "sell");
    quantity_from_double(50.0, 4, &events[2].quantity);
    price_from_double(12.0, 4, cny, &events[2].price);
    money_from_double(600.0, cny, &events[2].amount);

    int rc = mf_portfolio_rule_apply_trade_events(&item, 1, events, 3);
    assert(rc == 0);

    /* 两次卖出各盈利 100，累计已实现盈亏必须为 +200，绝不能因为成本池未递减而变成 -300 */
    assert(fabs(money_to_double(item.realized_pnl) - 200.0) < 1e-4);

    printf("PASS: test_portfolio_trade_events_partial_sells_cost_pool_decrement\n");
}

/* 1c. 关键回归测试：交易佣金/手续费必须正确计入已实现盈亏 */
static void test_portfolio_trade_events_with_fees(void) {
    currency_t cny = currency_from_str("CNY");

    mf_holding_item_t item = {0};
    item.asset_id = 102;
    snprintf(item.name, sizeof(item.name), "测试手续费标的");
    item.currency = cny;
    quantity_from_double(0.0, 4, &item.quantity);
    price_from_double(12.0, 4, cny, &item.net_value);
    money_from_double(0.0, cny, &item.cost_basis);

    mf_portfolio_trade_event_t events[2] = {0};

    /* Buy 100 @ 10 = 1000, fee = 10 (total cost = 1010) */
    events[0].asset_id = 102;
    snprintf(events[0].type, sizeof(events[0].type), "buy");
    quantity_from_double(100.0, 4, &events[0].quantity);
    price_from_double(10.0, 4, cny, &events[0].price);
    money_from_double(1000.0, cny, &events[0].amount);
    money_from_double(10.0, cny, &events[0].fee);

    /* Sell 100 @ 12 = 1200, fee = 10 (net proceeds = 1190) */
    events[1].asset_id = 102;
    snprintf(events[1].type, sizeof(events[1].type), "sell");
    quantity_from_double(100.0, 4, &events[1].quantity);
    price_from_double(12.0, 4, cny, &events[1].price);
    money_from_double(1200.0, cny, &events[1].amount);
    money_from_double(10.0, cny, &events[1].fee);

    int rc = mf_portfolio_rule_apply_trade_events(&item, 1, events, 2);
    assert(rc == 0);

    /* 买卖交易产生的 fee 计入成本与净变现：已实现盈亏 = (1200 - 10) - (1000 + 10) = 1190 - 1010 = +180 */
    assert(fabs(money_to_double(item.realized_pnl) - 180.0) < 1e-4);

    printf("PASS: test_portfolio_trade_events_with_fees\n");
}

/* 1d. 关键回归测试：持仓分析与绩效分析对佣金手续费与分红派息的已实现盈亏口径完全一致 */
static void test_holdings_pnl_includes_fees_and_dividends(void) {
    currency_t cny = currency_from_str("CNY");

    // 买入 100 股 @ 10 元，手续费 5 元 -> 成本基础 1005 元
    // 卖出 100 股 @ 12 元，手续费 8 元 -> 净变现 1192 元
    // 已实现盈亏应为 1192 - 1005 = 187 元
    // 加一笔分红 50 元 -> 187 + 50 = 237 元
    mf_holding_item_t item = {0};
    item.asset_id = 103;
    snprintf(item.name, sizeof(item.name), "测试分红与手续费标的");
    item.currency = cny;
    quantity_from_double(0.0, 4, &item.quantity);
    price_from_double(12.0, 4, cny, &item.net_value);
    money_from_double(0.0, cny, &item.cost_basis);

    mf_portfolio_trade_event_t events[3] = {0};

    /* 1. Buy 100 @ 10 = 1000, fee = 5 */
    events[0].asset_id = 103;
    snprintf(events[0].type, sizeof(events[0].type), "buy");
    quantity_from_double(100.0, 4, &events[0].quantity);
    price_from_double(10.0, 4, cny, &events[0].price);
    money_from_double(1000.0, cny, &events[0].amount);
    money_from_double(5.0, cny, &events[0].fee);

    /* 2. Sell 100 @ 12 = 1200, fee = 8 */
    events[1].asset_id = 103;
    snprintf(events[1].type, sizeof(events[1].type), "sell");
    quantity_from_double(100.0, 4, &events[1].quantity);
    price_from_double(12.0, 4, cny, &events[1].price);
    money_from_double(1200.0, cny, &events[1].amount);
    money_from_double(8.0, cny, &events[1].fee);

    /* 3. Dividend 50 */
    events[2].asset_id = 103;
    snprintf(events[2].type, sizeof(events[2].type), "dividend");
    money_from_double(50.0, cny, &events[2].amount);

    int rc = mf_portfolio_rule_apply_trade_events(&item, 1, events, 3);
    assert(rc == 0);

    /* 已实现盈亏应为 (1200 - 8) - (1000 + 5) + 50 = 1192 - 1005 + 50 = 237 */
    assert(fabs(money_to_double(item.realized_pnl) - 237.0) < 1e-4);

    printf("PASS: test_holdings_pnl_includes_fees_and_dividends\n");
}



/* 2. 历史兼容测试：同币种 Summary 汇总 */
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
    int rc = mf_portfolio_rule_aggregate_summary(items, 2, cny, NULL, &sum);
    assert(rc == 0);

    assert(money_to_double(sum.total_market_value) == 4000.0);
    assert(money_to_double(sum.total_cost_basis) == 3200.0);
    assert(money_to_double(sum.total_floating_pnl) == 800.0);
    assert(money_to_double(sum.total_realized_pnl) == 600.0);
    assert(sum.floating_pct == 25.0);

    printf("PASS: test_portfolio_summary_aggregation\n");
}

/* 2b. 关键回归测试：多币种持仓聚合汇总（通过显式汇率表折算为基准币种，杜绝 money_add 置零） */
static void test_portfolio_summary_multi_currency_aggregation(void) {
    currency_t cny = currency_from_str("CNY");
    currency_t usd = currency_from_str("USD");

    mf_fx_rate_table_t fx_table;
    mf_fx_rate_table_init(&fx_table);
    // 1 USD = 7.0 CNY
    assert(mf_fx_rate_table_add(&fx_table, usd, cny, r_make(7.0, usd, cny)) == 0);

    mf_holding_item_t items[2] = {0};
    // Item 0: CNY 标的
    items[0].asset_id = 1;
    items[0].currency = cny;
    money_from_double(1000.0, cny, &items[0].market_value);
    money_from_double(800.0, cny, &items[0].cost_basis);
    money_from_double(200.0, cny, &items[0].floating_pnl);
    money_from_double(100.0, cny, &items[0].realized_pnl);

    // Item 1: USD 标的
    items[1].asset_id = 2;
    items[1].currency = usd;
    money_from_double(100.0, usd, &items[1].market_value);
    money_from_double(80.0, usd, &items[1].cost_basis);
    money_from_double(20.0, usd, &items[1].floating_pnl);
    money_from_double(10.0, usd, &items[1].realized_pnl);

    mf_portfolio_summary_t sum = {0};
    int rc = mf_portfolio_rule_aggregate_summary(items, 2, cny, &fx_table, &sum);
    assert(rc == 0);

    // 市值 = 1000 + 100 * 7 = 1700 CNY
    assert(fabs(money_to_double(sum.total_market_value) - 1700.0) < 1e-2);
    // 成本 = 800 + 80 * 7 = 1360 CNY
    assert(fabs(money_to_double(sum.total_cost_basis) - 1360.0) < 1e-2);
    // 浮动盈亏 = 200 + 20 * 7 = 340 CNY
    assert(fabs(money_to_double(sum.total_floating_pnl) - 340.0) < 1e-2);
    // 已实现盈亏 = 100 + 10 * 7 = 170 CNY
    assert(fabs(money_to_double(sum.total_realized_pnl) - 170.0) < 1e-2);
    // 浮动盈亏率 = 340 / 1360 = 25%
    assert(fabs(sum.floating_pct - 25.0) < 1e-2);

    mf_fx_rate_table_free(&fx_table);
    printf("PASS: test_portfolio_summary_multi_currency_aggregation\n");
}


/* 3. 跨币种显式汇率折算与集中度风险度量 */
void test_portfolio_multi_currency_aggregation_and_risk() {
    currency_t usd = CURRENCY_USD;
    currency_t cny = CURRENCY_CNY;

    mf_fx_rate_table_t fx_table;
    mf_fx_rate_table_init(&fx_table);
    // 1 USD = 7.2 CNY
    assert(mf_fx_rate_table_add(&fx_table, usd, cny, r_make(7.2, usd, cny)) == 0);

    mf_position_t positions[2];
    memset(positions, 0, sizeof(positions));

    // Position 1: AAPL (USD)
    positions[0].asset_id = 1;
    positions[0].currency = usd;
    positions[0].cost_basis.total_cost = m_make(1000.0, usd);
    positions[0].valuation.market_value = m_make(1500.0, usd);
    positions[0].pnl.unrealized_pnl = m_make(500.0, usd);
    positions[0].pnl.realized_pnl = money_zero(usd);

    // Position 2: Moutai (CNY)
    positions[1].asset_id = 2;
    positions[1].currency = cny;
    positions[1].cost_basis.total_cost = m_make(1000.0, cny);
    positions[1].valuation.market_value = m_make(1440.0, cny);
    positions[1].pnl.unrealized_pnl = m_make(440.0, cny);
    positions[1].pnl.realized_pnl = m_make(100.0, cny);

    mf_portfolio_t pf;
    assert(mf_portfolio_aggregate(positions, 2, cny, &fx_table, &pf) == 0);

    // Total market value = 1500 * 7.2 + 1440 = 10800 + 1440 = 12240 CNY
    assert(fabs(money_to_double(pf.total_market_value) - 12240.0) < 1e-2);
    // Total cost basis = 1000 * 7.2 + 1000 = 7200 + 1000 = 8200 CNY
    assert(fabs(money_to_double(pf.total_cost_basis) - 8200.0) < 1e-2);
    // Total unrealized pnl = 500 * 7.2 + 440 = 3600 + 440 = 4040 CNY
    assert(fabs(money_to_double(pf.total_unrealized_pnl) - 4040.0) < 1e-2);
    // Total realized pnl = 100 CNY
    assert(fabs(money_to_double(pf.total_realized_pnl) - 100.0) < 1e-2);
    // Total pnl = 4040 + 100 = 4140 CNY
    assert(fabs(money_to_double(pf.total_pnl) - 4140.0) < 1e-2);

    // Allocation weights
    assert(fabs(pf.items[0].weight - (10800.0 / 12240.0)) < 1e-4);
    assert(fabs(pf.items[1].weight - (1440.0 / 12240.0)) < 1e-4);

    // Risk metrics: concentration
    assert(pf.max_holding_asset_id == 1);
    assert(fabs(pf.max_holding_weight - (10800.0 / 12240.0)) < 1e-4);

    double expected_hhi = pow(10800.0 / 12240.0, 2) + pow(1440.0 / 12240.0, 2);
    assert(fabs(pf.herfindahl_index - expected_hhi) < 1e-4);

    mf_portfolio_free(&pf);
    mf_fx_rate_table_free(&fx_table);
    printf("PASS: test_portfolio_multi_currency_aggregation_and_risk\n");
}

/* 4. 缺失汇率拒绝隐式折算 */
void test_portfolio_missing_fx_rate_fails() {
    currency_t usd = CURRENCY_USD;
    currency_t cny = CURRENCY_CNY;
    currency_t eur = CURRENCY_EUR;

    mf_fx_rate_table_t fx_table;
    mf_fx_rate_table_init(&fx_table);
    // Table only has USD -> CNY, missing EUR -> CNY
    assert(mf_fx_rate_table_add(&fx_table, usd, cny, r_make(7.2, usd, cny)) == 0);

    mf_position_t positions[2];
    memset(positions, 0, sizeof(positions));
    positions[0].asset_id = 1;
    positions[0].currency = usd;
    positions[0].valuation.market_value = m_make(100.0, usd);

    positions[1].asset_id = 2;
    positions[1].currency = eur;
    positions[1].valuation.market_value = m_make(100.0, eur);

    mf_portfolio_t pf;
    // Missing EUR -> CNY rate must cause aggregate to fail with -1 (strict safety)
    assert(mf_portfolio_aggregate(positions, 2, cny, &fx_table, &pf) == -1);

    mf_fx_rate_table_free(&fx_table);
    printf("PASS: test_portfolio_missing_fx_rate_fails\n");
}

int main() {
    test_portfolio_trade_events_and_pnl();
    test_portfolio_trade_events_partial_sells_cost_pool_decrement();
    test_portfolio_trade_events_with_fees();
    test_holdings_pnl_includes_fees_and_dividends();
    test_portfolio_summary_aggregation();
    test_portfolio_summary_multi_currency_aggregation();
    test_portfolio_multi_currency_aggregation_and_risk();
    test_portfolio_missing_fx_rate_fails();
    printf("All domain portfolio tests passed successfully!\n");
    return 0;
}

