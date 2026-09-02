#include "core/financial/decimal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    printf("=== Running Unit Test: Decimal Engine ===\n");

    /* 1. Exact representation: 0.1 + 0.2 == 0.3 */
    decimal_t d01, d02, d03, sum;
    assert(decimal_from_string("0.1", &d01) == DECIMAL_OK);
    assert(decimal_from_string("0.2", &d02) == DECIMAL_OK);
    assert(decimal_from_string("0.3", &d03) == DECIMAL_OK);
    assert(decimal_add(d01, d02, &sum) == DECIMAL_OK);
    assert(decimal_cmp(sum, d03) == 0);

    char buf[64];
    decimal_to_string(sum, buf, sizeof(buf));
    assert(strcmp(buf, "0.3") == 0);

    /* 2. Subtraction and negative values */
    decimal_t diff;
    assert(decimal_sub(d01, d02, &diff) == DECIMAL_OK);
    assert(decimal_is_negative(diff));
    decimal_to_string(diff, buf, sizeof(buf));
    assert(strcmp(buf, "-0.1") == 0);

    /* 3. Multiplication */
    decimal_t price, qty, total;
    assert(decimal_from_string("19.99", &price) == DECIMAL_OK);
    assert(decimal_from_string("100", &qty) == DECIMAL_OK);
    assert(decimal_mul(price, qty, &total) == DECIMAL_OK);
    decimal_to_string_fixed(total, 2, buf, sizeof(buf));
    assert(strcmp(buf, "1999.00") == 0);

    /* 4. Division with scale and Banker's rounding */
    decimal_t d1, d3, div_res;
    assert(decimal_from_string("1.0", &d1) == DECIMAL_OK);
    assert(decimal_from_string("3.0", &d3) == DECIMAL_OK);
    assert(decimal_div(d1, d3, 4, ROUND_HALF_UP, &div_res) == DECIMAL_OK);
    decimal_to_string(div_res, buf, sizeof(buf));
    assert(strcmp(buf, "0.3333") == 0);

    /* Division by zero */
    decimal_t zero = decimal_zero();
    assert(decimal_div(d1, zero, 2, ROUND_HALF_UP, &div_res) == DECIMAL_ERR_DIV_BY_ZERO);

    /* 5. Rounding modes */
    decimal_t d_val, r_up, r_even, r_down;
    assert(decimal_from_string("2.5", &d_val) == DECIMAL_OK);
    assert(decimal_round(d_val, 0, ROUND_HALF_UP, &r_up) == DECIMAL_OK);
    assert(decimal_cmp(r_up, decimal_from_int(3)) == 0); /* 2.5 -> 3 */

    assert(decimal_round(d_val, 0, ROUND_HALF_EVEN, &r_even) == DECIMAL_OK);
    assert(decimal_cmp(r_even, decimal_from_int(2)) == 0); /* 2.5 -> 2 (even) */

    decimal_t d_val2;
    assert(decimal_from_string("3.5", &d_val2) == DECIMAL_OK);
    assert(decimal_round(d_val2, 0, ROUND_HALF_EVEN, &r_even) == DECIMAL_OK);
    assert(decimal_cmp(r_even, decimal_from_int(4)) == 0); /* 3.5 -> 4 (even) */

    assert(decimal_from_string("2.99", &d_val) == DECIMAL_OK);
    assert(decimal_round(d_val, 0, ROUND_DOWN, &r_down) == DECIMAL_OK);
    assert(decimal_cmp(r_down, decimal_from_int(2)) == 0);

    /* 6. Extreme value handling */
    decimal_t big1, big2, big_sum;
    assert(decimal_from_string("1000000000000000.50", &big1) == DECIMAL_OK); /* 10^15 */
    assert(decimal_from_string("2000000000000000.25", &big2) == DECIMAL_OK);
    assert(decimal_add(big1, big2, &big_sum) == DECIMAL_OK);
    decimal_to_string_fixed(big_sum, 2, buf, sizeof(buf));
    assert(strcmp(buf, "3000000000000000.75") == 0);

    /* Micro precision (crypto scale 8) */
    decimal_t btc1, btc2, btc_sum;
    assert(decimal_from_string("0.00000001", &btc1) == DECIMAL_OK);
    assert(decimal_from_string("0.00000002", &btc2) == DECIMAL_OK);
    assert(decimal_add(btc1, btc2, &btc_sum) == DECIMAL_OK);
    decimal_to_string_fixed(btc_sum, 8, buf, sizeof(buf));
    assert(strcmp(buf, "0.00000003") == 0);

    printf("✓ All decimal arithmetic & rounding tests passed successfully!\n");
    return 0;
}
