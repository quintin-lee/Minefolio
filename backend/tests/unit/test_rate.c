#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "core/financial/money.h"
#include "core/financial/percentage.h"
#include "core/financial/rate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    printf("=== Running Unit Test: Rate & Percentage ===\n");

    /* 1. Foreign exchange rate conversion: USD -> CNY */
    rate_t usd_to_cny;
    assert(rate_from_string("7.1500", CURRENCY_USD, CURRENCY_CNY, &usd_to_cny) == DECIMAL_OK);

    money_t usd_100;
    assert(money_from_string("100.00", CURRENCY_USD, &usd_100) == DECIMAL_OK);

    money_t cny_res;
    assert(rate_convert_money(usd_100, usd_to_cny, &cny_res) == DECIMAL_OK);
    char buf[64];
    money_to_string(cny_res, buf, sizeof(buf));
    assert(strcmp(buf, "715.00") == 0);
    assert(currency_equals(cny_res.currency, CURRENCY_CNY));

    /* Inverted rate (CNY -> USD) */
    rate_t cny_to_usd;
    assert(rate_invert(usd_to_cny, 6, &cny_to_usd) == DECIMAL_OK);
    rate_to_string_fixed(cny_to_usd, 6, buf, sizeof(buf));
    assert(strcmp(buf, "0.139860") == 0);

    /* 2. Triangular rate chain: EUR -> USD -> CNY */
    rate_t eur_to_usd;
    assert(rate_from_string("1.0800", CURRENCY_EUR, CURRENCY_USD, &eur_to_usd) == DECIMAL_OK);

    rate_t eur_to_cny;
    assert(rate_chain(eur_to_usd, usd_to_cny, 4, &eur_to_cny) == DECIMAL_OK);
    rate_to_string_fixed(eur_to_cny, 4, buf, sizeof(buf));
    assert(strcmp(buf, "7.7220") == 0);
    assert(currency_equals(eur_to_cny.from_currency, CURRENCY_EUR));
    assert(currency_equals(eur_to_cny.to_currency, CURRENCY_CNY));

    /* 3. Percentage application: 15.5% tax on 200 CNY */
    percentage_t tax_rate;
    assert(percentage_from_string("15.5", &tax_rate) == DECIMAL_OK);

    money_t base_m;
    assert(money_from_string("200.00", CURRENCY_CNY, &base_m) == DECIMAL_OK);

    money_t tax_m;
    assert(percentage_apply(base_m, tax_rate, &tax_m) == DECIMAL_OK);
    money_to_string(tax_m, buf, sizeof(buf));
    assert(strcmp(buf, "31.00") == 0);

    /* 4. Percentage calculation: 31 / 200 * 100 = 15.50% */
    percentage_t calc_pct;
    assert(percentage_calc(tax_m, base_m, 2, ROUND_HALF_UP, &calc_pct) == DECIMAL_OK);
    percentage_to_string_fixed(calc_pct, 2, buf, sizeof(buf));
    assert(strcmp(buf, "15.50") == 0);

    printf("✓ All rate and percentage tests passed successfully!\n");
    return 0;
}
