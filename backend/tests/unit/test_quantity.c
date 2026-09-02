#include "core/financial/quantity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    printf("=== Running Unit Test: Quantity Model ===\n");

    quantity_t q1, q2, q_sum, q_diff;
    assert(quantity_from_string("100.25", &q1) == DECIMAL_OK);
    assert(quantity_from_string("50.75", &q2) == DECIMAL_OK);

    assert(quantity_add(q1, q2, &q_sum) == DECIMAL_OK);
    char buf[64];
    quantity_to_string_fixed(q_sum, 2, buf, sizeof(buf));
    assert(strcmp(buf, "151.00") == 0);

    assert(quantity_sub(q1, q2, &q_diff) == DECIMAL_OK);
    quantity_to_string_fixed(q_diff, 2, buf, sizeof(buf));
    assert(strcmp(buf, "49.50") == 0);

    /* Crypto fractional quantity */
    quantity_t btc_q1, btc_q2, btc_sum;
    assert(quantity_from_string("0.05000000", &btc_q1) == DECIMAL_OK);
    assert(quantity_from_string("0.02500000", &btc_q2) == DECIMAL_OK);
    assert(quantity_add(btc_q1, btc_q2, &btc_sum) == DECIMAL_OK);
    quantity_to_string_fixed(btc_sum, 8, buf, sizeof(buf));
    assert(strcmp(buf, "0.07500000") == 0);

    printf("✓ All quantity domain tests passed successfully!\n");
    return 0;
}
