#include "domain/portfolio/fx_table.h"
#include <assert.h>
#include <stdio.h>
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

void test_identity_conversion() {
    currency_t usd = CURRENCY_USD;
    money_t src = m_make(100.0, usd);
    money_t out = money_zero(usd);

    assert(mf_fx_convert_money(src, usd, NULL, &out) == 0);
    assert(currency_equals(out.currency, usd));
    assert(fabs(money_to_double(out) - 100.0) < 1e-4);

    printf("PASS: test_identity_conversion\n");
}

void test_explicit_fx_conversion() {
    currency_t usd = CURRENCY_USD;
    currency_t cny = CURRENCY_CNY;

    mf_fx_rate_table_t table;
    mf_fx_rate_table_init(&table);

    // 1 USD = 7.2000 CNY
    assert(mf_fx_rate_table_add(&table, usd, cny, r_make(7.2, usd, cny)) == 0);

    money_t src = m_make(100.0, usd);
    money_t out = money_zero(cny);

    assert(mf_fx_convert_money(src, cny, &table, &out) == 0);
    assert(currency_equals(out.currency, cny));
    assert(fabs(money_to_double(out) - 720.0) < 1e-4);

    // Inverse conversion: 720 CNY -> USD
    money_t src_cny = m_make(720.0, cny);
    money_t out_usd = money_zero(usd);
    assert(mf_fx_convert_money(src_cny, usd, &table, &out_usd) == 0);
    assert(currency_equals(out_usd.currency, usd));
    assert(fabs(money_to_double(out_usd) - 100.0) < 0.05);

    mf_fx_rate_table_free(&table);
    printf("PASS: test_explicit_fx_conversion\n");
}

void test_missing_rate_strictly_fails() {
    currency_t usd = CURRENCY_USD;
    currency_t cny = CURRENCY_CNY;
    currency_t eur = CURRENCY_EUR;

    mf_fx_rate_table_t table;
    mf_fx_rate_table_init(&table);

    // Table only has USD -> CNY
    assert(mf_fx_rate_table_add(&table, usd, cny, r_make(7.2, usd, cny)) == 0);

    money_t src_eur = m_make(100.0, eur);
    money_t out = money_zero(cny);

    // Converting EUR to CNY must return -1 (strictly rejected, NO implicit 1:1)
    assert(mf_fx_convert_money(src_eur, cny, &table, &out) == -1);

    mf_fx_rate_table_free(&table);
    printf("PASS: test_missing_rate_strictly_fails\n");
}

int main() {
    test_identity_conversion();
    test_explicit_fx_conversion();
    test_missing_rate_strictly_fails();
    printf("All domain multi-currency tests passed!\n");
    return 0;
}
