#include "core/financial/currency.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    printf("=== Running Unit Test: Currency ===\n");

    currency_t c1 = currency_from_str("cny");
    assert(currency_equals(c1, CURRENCY_CNY));
    assert(strcmp(currency_code(&c1), "CNY") == 0);
    assert(currency_precision(c1) == 2);
    assert(currency_is_valid(c1));

    currency_t c2 = currency_from_str("USD");
    assert(currency_equals(c2, CURRENCY_USD));
    assert(!currency_equals(c1, c2));

    currency_t jpy = currency_from_str("jpy");
    assert(currency_precision(jpy) == 0);

    currency_t btc = currency_from_str("btc");
    assert(currency_precision(btc) == 8);

    currency_t empty = currency_from_str("");
    assert(!currency_is_valid(empty));
    assert(currency_equals(empty, CURRENCY_CNY)); /* Empty defaults to CNY in equality comparison */

    printf("✓ All currency tests passed successfully!\n");
    return 0;
}
