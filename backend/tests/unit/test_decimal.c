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

    /* 7. Division rounding comprehensive test */
    /* 7.1 Banker's rounding exact ties */
    decimal_t d_one, d_two, d_three, d_five, d_seven, res;
    assert(decimal_from_string("1.0", &d_one) == DECIMAL_OK);
    assert(decimal_from_string("2.0", &d_two) == DECIMAL_OK);
    assert(decimal_from_string("3.0", &d_three) == DECIMAL_OK);
    assert(decimal_from_string("5.0", &d_five) == DECIMAL_OK);
    assert(decimal_from_string("7.0", &d_seven) == DECIMAL_OK);

    assert(decimal_div(d_one, d_two, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(0)) == 0); /* 0.5 -> 0 (even) */

    assert(decimal_div(d_three, d_two, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(2)) == 0); /* 1.5 -> 2 (even) */

    assert(decimal_div(d_five, d_two, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(2)) == 0); /* 2.5 -> 2 (even) */

    assert(decimal_div(d_seven, d_two, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(4)) == 0); /* 3.5 -> 4 (even) */

    /* 7.2 Banker's rounding non-ties (> 0.5 and < 0.5) */
    decimal_t d_2001, d_800;
    assert(decimal_from_string("2001", &d_2001) == DECIMAL_OK);
    assert(decimal_from_string("800", &d_800) == DECIMAL_OK);
    /* 2001 / 800 = 2.50125 > 2.5, must round to 3, not 2 */
    assert(decimal_div(d_2001, d_800, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(3)) == 0);

    decimal_t d_10001, d_9999, d_20000;
    assert(decimal_from_string("10001", &d_10001) == DECIMAL_OK);
    assert(decimal_from_string("9999", &d_9999) == DECIMAL_OK);
    assert(decimal_from_string("20000", &d_20000) == DECIMAL_OK);
    /* 10001 / 20000 = 0.50005 > 0.5, must round to 1 */
    assert(decimal_div(d_10001, d_20000, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(1)) == 0);
    /* 9999 / 20000 = 0.49995 < 0.5, must round to 0 */
    assert(decimal_div(d_9999, d_20000, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(0)) == 0);

    /* 7.3 Negative Banker's rounding */
    decimal_t d_neg_one, d_neg_three, d_neg_five, d_neg_seven, d_neg_2001;
    assert(decimal_from_string("-1.0", &d_neg_one) == DECIMAL_OK);
    assert(decimal_from_string("-3.0", &d_neg_three) == DECIMAL_OK);
    assert(decimal_from_string("-5.0", &d_neg_five) == DECIMAL_OK);
    assert(decimal_from_string("-7.0", &d_neg_seven) == DECIMAL_OK);
    assert(decimal_from_string("-2001", &d_neg_2001) == DECIMAL_OK);

    assert(decimal_div(d_neg_one, d_two, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(0)) == 0); /* -0.5 -> 0 */

    assert(decimal_div(d_neg_three, d_two, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(-2)) == 0); /* -1.5 -> -2 */

    assert(decimal_div(d_neg_five, d_two, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(-2)) == 0); /* -2.5 -> -2 */

    assert(decimal_div(d_neg_seven, d_two, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(-4)) == 0); /* -3.5 -> -4 */

    assert(decimal_div(d_neg_2001, d_800, 0, ROUND_HALF_EVEN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(-3)) == 0); /* -2.50125 -> -3 */

    /* 7.4 ROUND_UP with small remainder beyond 1 extra digit */
    decimal_t d_300;
    assert(decimal_from_string("300", &d_300) == DECIMAL_OK);
    /* 1 / 300 = 0.00333... rounded UP at scale 0 must be 1 */
    assert(decimal_div(d_one, d_300, 0, ROUND_UP, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(1)) == 0);

    /* -1 / 300 rounded UP (away from zero) at scale 0 must be -1 */
    assert(decimal_div(d_neg_one, d_300, 0, ROUND_UP, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(-1)) == 0);

    /* 7.5 ROUND_CEIL and ROUND_FLOOR */
    assert(decimal_div(d_one, d_300, 0, ROUND_CEIL, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(1)) == 0);
    assert(decimal_div(d_neg_one, d_300, 0, ROUND_CEIL, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(0)) == 0);

    assert(decimal_div(d_one, d_300, 0, ROUND_FLOOR, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(0)) == 0);
    assert(decimal_div(d_neg_one, d_300, 0, ROUND_FLOOR, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(-1)) == 0);

    /* 7.6 Shift < 0 (divisor scaled up) */
    decimal_t d_small_a, d_large_b;
    assert(decimal_from_string("0.0000000001", &d_small_a) == DECIMAL_OK); /* scale 10 */
    assert(decimal_from_string("100", &d_large_b) == DECIMAL_OK); /* scale 0 */
    assert(decimal_div(d_small_a, d_large_b, 0, ROUND_UP, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(1)) == 0);
    assert(decimal_div(d_small_a, d_large_b, 0, ROUND_DOWN, &res) == DECIMAL_OK);
    assert(decimal_cmp(res, decimal_from_int(0)) == 0);

    /* 8. Decimal overflow handling */
    /* 8.1 Addition overflow */
    decimal_t max_dec, one_dec, oflow_res;
    /* 170141183460469231731687303715884105727 is 2^127 - 1 */
    assert(decimal_from_string("170141183460469231731687303715884105727", &max_dec) == DECIMAL_OK);
    assert(decimal_from_string("1", &one_dec) == DECIMAL_OK);
    assert(decimal_add(max_dec, one_dec, &oflow_res) == DECIMAL_ERR_OVERFLOW);

    /* Alignment scale overflow */
    decimal_t big_scale0, tiny_scale18;
    assert(decimal_from_string("100000000000000000000000000000", &big_scale0) == DECIMAL_OK); /* 10^29, scale 0 */
    assert(decimal_from_string("0.000000000000000001", &tiny_scale18) == DECIMAL_OK); /* scale 18 */
    /* Aligning 10^29 to scale 18 requires 10^29 * 10^18 = 10^47, exceeding 128 bits */
    assert(decimal_add(big_scale0, tiny_scale18, &oflow_res) == DECIMAL_ERR_OVERFLOW);
    assert(decimal_sub(big_scale0, tiny_scale18, &oflow_res) == DECIMAL_ERR_OVERFLOW);

    /* 8.2 Subtraction overflow */
    decimal_t min_dec;
    assert(decimal_from_string("-170141183460469231731687303715884105728", &min_dec) == DECIMAL_OK);
    assert(decimal_sub(min_dec, one_dec, &oflow_res) == DECIMAL_ERR_OVERFLOW);

    /* 8.3 Multiplication overflow */
    decimal_t mul_a, mul_b;
    assert(decimal_from_string("100000000000000000000", &mul_a) == DECIMAL_OK); /* 10^20 */
    assert(decimal_from_string("100000000000000000000", &mul_b) == DECIMAL_OK); /* 10^20 */
    /* 10^20 * 10^20 = 10^40 > 2^127 - 1 */
    assert(decimal_mul(mul_a, mul_b, &oflow_res) == DECIMAL_ERR_OVERFLOW);

    /* 8.4 Division overflow (huge quotient) */
    decimal_t div_a, div_b;
    assert(decimal_from_string("100000000000000000000000000000", &div_a) == DECIMAL_OK); /* 10^29, scale 0 */
    assert(decimal_from_string("0.0000000001", &div_b) == DECIMAL_OK); /* 10^-10 */
    /* Result at target scale 18: 10^29 / 10^-10 * 10^18 = 10^57, overflows 128-bit */
    assert(decimal_div(div_a, div_b, 18, ROUND_HALF_UP, &oflow_res) == DECIMAL_ERR_OVERFLOW);

    /* 8.5 String parse overflow */
    decimal_t str_oflow;
    assert(decimal_from_string("99999999999999999999999999999999999999999999999999", &str_oflow) == DECIMAL_ERR_OVERFLOW);

    /* 8.6 Large intermediate operations that fit without premature overflow */
    /* 20.0 (scale 18) * 30.0 (scale 18) = 600.0 (scale 18) */
    decimal_t mul_p1, mul_p2, mul_res;
    assert(decimal_from_string("20.000000000000000000", &mul_p1) == DECIMAL_OK);
    assert(decimal_from_string("30.000000000000000000", &mul_p2) == DECIMAL_OK);
    assert(decimal_mul(mul_p1, mul_p2, &mul_res) == DECIMAL_OK);
    decimal_to_string_fixed(mul_res, 2, buf, sizeof(buf));
    assert(strcmp(buf, "600.00") == 0);

    /* Intermediate division shift that does not overflow: 10^25 / 10^20 with target_scale 10 */
    decimal_t div_large1, div_large2, div_large_res;
    assert(decimal_from_string("10000000000000000000000000", &div_large1) == DECIMAL_OK); /* 10^25 */
    assert(decimal_from_string("100000000000000000000", &div_large2) == DECIMAL_OK);      /* 10^20 */
    assert(decimal_div(div_large1, div_large2, 10, ROUND_HALF_UP, &div_large_res) == DECIMAL_OK);
    decimal_to_string_fixed(div_large_res, 0, buf, sizeof(buf));
    assert(strcmp(buf, "100000") == 0);

    /* 8.7 Safe decimal_cmp without overflow */
    assert(decimal_cmp(max_dec, tiny_scale18) > 0);
    assert(decimal_cmp(tiny_scale18, max_dec) < 0);
    assert(decimal_cmp(min_dec, tiny_scale18) < 0);

    printf("✓ All decimal arithmetic & rounding tests passed successfully!\n");
    return 0;
}
