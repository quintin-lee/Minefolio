#include "core/financial/price.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    printf("=== Running Unit Test: Price Model & Dimensional Operations ===\n");

    /* 1. Price * Quantity = Money */
    price_t p;
    quantity_t q;
    money_t m;
    assert(price_from_string("15.50", CURRENCY_CNY, &p) == DECIMAL_OK);
    assert(quantity_from_string("1000", &q) == DECIMAL_OK);
    assert(price_times_quantity(p, q, &m) == DECIMAL_OK);

    char buf[64];
    money_to_string(m, buf, sizeof(buf));
    assert(strcmp(buf, "15500.00") == 0);
    assert(currency_equals(m.currency, CURRENCY_CNY));

    /* 2. Money / Quantity = Price */
    price_t calc_p;
    assert(money_div_quantity(m, q, 2, ROUND_HALF_UP, &calc_p) == DECIMAL_OK);
    price_to_string_fixed(calc_p, 2, buf, sizeof(buf));
    assert(strcmp(buf, "15.50") == 0);

    /* 3. Money / Price = Quantity */
    quantity_t calc_q;
    assert(money_div_price(m, p, 2, ROUND_HALF_UP, &calc_q) == DECIMAL_OK);
    quantity_to_string_fixed(calc_q, 2, buf, sizeof(buf));
    assert(strcmp(buf, "1000.00") == 0);

    /* 4. Division by zero check */
    quantity_t zero_q = quantity_zero();
    assert(money_div_quantity(m, zero_q, 2, ROUND_HALF_UP, &calc_p) == DECIMAL_ERR_DIV_BY_ZERO);

    printf("✓ All price and dimensional tests passed successfully!\n");
    return 0;
}
