#include "core/financial/money.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    printf("=== Running Unit Test: Money Model ===\n");

    /* 1. Same-currency addition & subtraction */
    money_t m1, m2, m_sum, m_diff;
    assert(money_from_string("100.50", CURRENCY_CNY, &m1) == DECIMAL_OK);
    assert(money_from_string("50.25", CURRENCY_CNY, &m2) == DECIMAL_OK);

    assert(money_add(m1, m2, &m_sum) == DECIMAL_OK);
    char buf[64];
    money_to_string(m_sum, buf, sizeof(buf));
    assert(strcmp(buf, "150.75") == 0);

    assert(money_sub(m1, m2, &m_diff) == DECIMAL_OK);
    money_to_string(m_diff, buf, sizeof(buf));
    assert(strcmp(buf, "50.25") == 0);

    /* 2. Currency mismatch safety check */
    money_t m_usd;
    assert(money_from_string("50.00", CURRENCY_USD, &m_usd) == DECIMAL_OK);
    assert(money_add(m1, m_usd, &m_sum) == DECIMAL_ERR_INVALID_ARG);
    assert(money_sub(m1, m_usd, &m_diff) == DECIMAL_ERR_INVALID_ARG);

    /* 3. JPY Zero precision test */
    money_t m_jpy;
    assert(money_from_string("1250", CURRENCY_JPY, &m_jpy) == DECIMAL_OK);
    money_to_string(m_jpy, buf, sizeof(buf));
    assert(strcmp(buf, "1250") == 0);

    /* 4. Comparison and unary operations */
    assert(money_cmp(m1, m2) > 0);
    assert(money_cmp(m2, m1) < 0);
    assert(money_cmp(m1, m1) == 0);

    money_t neg_m1 = money_neg(m1);
    assert(money_is_negative(neg_m1));
    assert(money_is_positive(m1));

    money_t abs_m1 = money_abs(neg_m1);
    assert(money_cmp(abs_m1, m1) == 0);

    printf("✓ All money domain tests passed successfully!\n");
    return 0;
}
