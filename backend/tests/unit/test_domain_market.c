#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "domain/market/entity.h"
#include "domain/market/rules.h"
#include "core/financial/currency.h"

static void test_market_sync_delta(void) {
    currency_t cny = currency_from_str("CNY");
    price_t p_old, p_new;
    quantity_t q;
    money_t delta;

    /* 1. 正常上涨: 100 股 从 2.0 涨至 2.5 -> delta = +50 */
    price_from_double(2.0, 4, cny, &p_old);
    price_from_double(2.5, 4, cny, &p_new);
    quantity_from_double(100.0, 4, &q);

    int rc = mf_market_rule_calc_sync_delta(p_old, p_new, q, cny, &delta);
    assert(rc == 0);
    assert(money_to_double(delta) == 50.0);

    /* 2. 正常下跌: 100 股 从 2.5 跌至 2.0 -> delta = -50 */
    rc = mf_market_rule_calc_sync_delta(p_new, p_old, q, cny, &delta);
    assert(rc == 0);
    assert(money_to_double(delta) == -50.0);

    /* 3. 持仓为 0 时 delta 为 0 */
    quantity_from_double(0.0, 4, &q);
    rc = mf_market_rule_calc_sync_delta(p_old, p_new, q, cny, &delta);
    assert(rc == 0);
    assert(money_to_double(delta) == 0.0);

    printf("PASS: test_market_sync_delta\n");
}

static void test_currency_conversion(void) {
    currency_t usd = currency_from_str("USD");
    currency_t cny = currency_from_str("CNY");

    money_t usd_amt;
    money_from_double(100.0, usd, &usd_amt);

    money_t cny_amt;
    int rc = mf_market_rule_convert_currency(usd_amt, 7.20, cny, &cny_amt);
    assert(rc == 0);
    assert(money_to_double(cny_amt) == 720.0);

    printf("PASS: test_currency_conversion\n");
}

static void test_quote_and_settings_validation(void) {
    currency_t cny = currency_from_str("CNY");
    char err[256];

    mf_market_quote_t q = {0};
    snprintf(q.symbol, sizeof(q.symbol), "sh600519");
    price_from_double(1750.0, 4, cny, &q.current_price);
    assert(mf_market_rule_validate_quote(&q, err, sizeof(err)) == 0);

    /* 空 symbol 校验 */
    q.symbol[0] = '\0';
    assert(mf_market_rule_validate_quote(&q, err, sizeof(err)) != 0);

    /* 配置校验 */
    mf_market_settings_t s = {0};
    s.market_sync_interval_min = 30;
    assert(mf_market_rule_validate_settings(&s, err, sizeof(err)) == 0);

    s.market_sync_interval_min = 0;
    assert(mf_market_rule_validate_settings(&s, err, sizeof(err)) != 0);

    printf("PASS: test_quote_and_settings_validation\n");
}

int main(void) {
    test_market_sync_delta();
    test_currency_conversion();
    test_quote_and_settings_validation();
    printf("All domain market tests passed successfully!\n");
    return 0;
}
