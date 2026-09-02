#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "core/financial/money.h"
#include "core/financial/percentage.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    printf("=== Running Unit Test: PnL & Position Accounting ===\n");

    /* 1. Buy Lot 1: 1000 shares @ 10.00 CNY, fee 5.00 */
    quantity_t q1, q2;
    price_t    p1, p2;
    money_t    fee1, fee2;
    assert(quantity_from_string("1000", &q1) == DECIMAL_OK);
    assert(price_from_string("10.00", CURRENCY_CNY, &p1) == DECIMAL_OK);
    assert(money_from_string("5.00", CURRENCY_CNY, &fee1) == DECIMAL_OK);

    money_t m1;
    assert(price_times_quantity(p1, q1, &m1) == DECIMAL_OK);
    money_t cost1;
    assert(money_add(m1, fee1, &cost1) == DECIMAL_OK);

    char buf[64];
    money_to_string(cost1, buf, sizeof(buf));
    assert(strcmp(buf, "10005.00") == 0);

    /* 2. Buy Lot 2: 500 shares @ 12.00 CNY, fee 3.00 */
    assert(quantity_from_string("500", &q2) == DECIMAL_OK);
    assert(price_from_string("12.00", CURRENCY_CNY, &p2) == DECIMAL_OK);
    assert(money_from_string("3.00", CURRENCY_CNY, &fee2) == DECIMAL_OK);

    money_t m2;
    assert(price_times_quantity(p2, q2, &m2) == DECIMAL_OK);
    money_t cost2;
    assert(money_add(m2, fee2, &cost2) == DECIMAL_OK);

    /* Total position after 2 buys */
    quantity_t total_qty;
    money_t    total_cost;
    assert(quantity_add(q1, q2, &total_qty) == DECIMAL_OK);
    assert(money_add(cost1, cost2, &total_cost) == DECIMAL_OK);

    quantity_to_string_fixed(total_qty, 0, buf, sizeof(buf));
    assert(strcmp(buf, "1500") == 0);

    money_to_string(total_cost, buf, sizeof(buf));
    assert(strcmp(buf, "16008.00") == 0);

    /* Weighted average cost per share: 16008.00 / 1500 = 10.6720 */
    price_t avg_cost;
    assert(money_div_quantity(total_cost, total_qty, 4, ROUND_HALF_UP, &avg_cost) == DECIMAL_OK);
    price_to_string_fixed(avg_cost, 4, buf, sizeof(buf));
    assert(strcmp(buf, "10.6720") == 0);

    /* 3. Partial sell: 600 shares @ 15.00 CNY, fee 4.00 */
    quantity_t sell_q;
    price_t    sell_p;
    money_t    sell_fee;
    assert(quantity_from_string("600", &sell_q) == DECIMAL_OK);
    assert(price_from_string("15.00", CURRENCY_CNY, &sell_p) == DECIMAL_OK);
    assert(money_from_string("4.00", CURRENCY_CNY, &sell_fee) == DECIMAL_OK);

    money_t gross_sell;
    assert(price_times_quantity(sell_p, sell_q, &gross_sell) == DECIMAL_OK);
    money_t net_sell;
    assert(money_sub(gross_sell, sell_fee, &net_sell) == DECIMAL_OK);
    money_to_string(net_sell, buf, sizeof(buf));
    assert(strcmp(buf, "8996.00") == 0);

    /* Proportional cost reduction: 600 * 10.6720 = 6403.20 */
    money_t cost_reduction;
    assert(price_times_quantity(avg_cost, sell_q, &cost_reduction) == DECIMAL_OK);
    money_to_string(cost_reduction, buf, sizeof(buf));
    assert(strcmp(buf, "6403.20") == 0);

    /* Realized PnL = 8996.00 - 6403.20 = 2592.80 */
    money_t realized_pnl;
    assert(money_sub(net_sell, cost_reduction, &realized_pnl) == DECIMAL_OK);
    money_to_string(realized_pnl, buf, sizeof(buf));
    assert(strcmp(buf, "2592.80") == 0);

    /* Remaining position: 900 shares, remaining cost: 16008.00 - 6403.20 = 9604.80 */
    quantity_t rem_qty;
    money_t    rem_cost;
    assert(quantity_sub(total_qty, sell_q, &rem_qty) == DECIMAL_OK);
    assert(money_sub(total_cost, cost_reduction, &rem_cost) == DECIMAL_OK);

    quantity_to_string_fixed(rem_qty, 0, buf, sizeof(buf));
    assert(strcmp(buf, "900") == 0);

    money_to_string(rem_cost, buf, sizeof(buf));
    assert(strcmp(buf, "9604.80") == 0);

    /* 4. Unrealized Floating PnL at market price 14.50 */
    price_t current_net;
    assert(price_from_string("14.50", CURRENCY_CNY, &current_net) == DECIMAL_OK);

    money_t current_val;
    assert(price_times_quantity(current_net, rem_qty, &current_val) == DECIMAL_OK);
    money_to_string(current_val, buf, sizeof(buf));
    assert(strcmp(buf, "13050.00") == 0);

    money_t unrealized_pnl;
    assert(money_sub(current_val, rem_cost, &unrealized_pnl) == DECIMAL_OK);
    money_to_string(unrealized_pnl, buf, sizeof(buf));
    assert(strcmp(buf, "3445.20") == 0);

    printf("✓ All PnL accounting & weighted cost tests passed successfully!\n");
    return 0;
}
