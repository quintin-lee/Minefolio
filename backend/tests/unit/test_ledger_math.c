#include "core/ledger/ledger_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
test_buy_math(void)
{
    printf("  Testing Buy Position Math...\n");
    // Initial Buy 1000 shares @ $10.00 with $5.00 fee -> Total cost: $10005.00
    quantity_t q0 = quantity_zero();
    money_t c0 = money_zero(CURRENCY_USD);

    quantity_t buy1_q;
    quantity_from_string("1000", &buy1_q);
    price_t buy1_p;
    price_from_string("10.00", CURRENCY_USD, &buy1_p);
    money_t buy1_fee;
    money_from_string("5.00", CURRENCY_USD, &buy1_fee);

    quantity_t pos1_q;
    money_t pos1_c;
    int rc = ledger_calc_buy_position(q0, c0, buy1_q, buy1_p, buy1_fee, &pos1_q, &pos1_c);
    assert(rc == 0);

    char buf[64];
    quantity_to_string_fixed(pos1_q, 0, buf, sizeof(buf));
    assert(strcmp(buf, "1000") == 0);
    money_to_string(pos1_c, buf, sizeof(buf));
    assert(strcmp(buf, "10005.00") == 0);

    // Second Buy 500 shares @ $14.00 with $3.00 fee (Amount 7000 + 3 = 7003) -> Total shares: 1500, Total cost: 17008.00
    quantity_t buy2_q;
    quantity_from_string("500", &buy2_q);
    price_t buy2_p;
    price_from_string("14.00", CURRENCY_USD, &buy2_p);
    money_t buy2_fee;
    money_from_string("3.00", CURRENCY_USD, &buy2_fee);

    quantity_t pos2_q;
    money_t pos2_c;
    rc = ledger_calc_buy_position(pos1_q, pos1_c, buy2_q, buy2_p, buy2_fee, &pos2_q, &pos2_c);
    assert(rc == 0);
    quantity_to_string_fixed(pos2_q, 0, buf, sizeof(buf));
    assert(strcmp(buf, "1500") == 0);
    money_to_string(pos2_c, buf, sizeof(buf));
    assert(strcmp(buf, "17008.00") == 0);
    printf("    ✓ Buy position math passed.\n");
}

static void
test_sell_math(void)
{
    printf("  Testing Sell Position Math...\n");
    // Start with 1500 shares @ $17008.00 cost (Average cost per share = 17008 / 1500 = 11.338667)
    quantity_t pos_q;
    quantity_from_string("1500", &pos_q);
    money_t pos_c;
    money_from_string("17008.00", CURRENCY_USD, &pos_c);

    // Partial Sell 600 shares @ $15.00 with $10.00 fee
    // Sell Gross = 600 * 15 = 9000.00
    // Sell Net = 9000 - 10 = 8990.00
    // Cost Reduction = (600 / 1500) * 17008.00 = 0.4 * 17008.00 = 6803.20
    // Remaining Quantity = 1500 - 600 = 900
    // Remaining Cost = 17008.00 - 6803.20 = 10204.80
    // Realized PnL = 8990.00 - 6803.20 = +2186.80
    quantity_t sell_q;
    quantity_from_string("600", &sell_q);
    price_t sell_p;
    price_from_string("15.00", CURRENCY_USD, &sell_p);
    money_t sell_fee;
    money_from_string("10.00", CURRENCY_USD, &sell_fee);

    quantity_t rem_q;
    money_t rem_c;
    money_t cost_red;
    money_t realized_pnl;
    int rc = ledger_calc_sell_position(pos_q, pos_c, sell_q, sell_p, sell_fee,
                                       &rem_q, &rem_c, &cost_red, &realized_pnl);
    assert(rc == 0);

    char buf[64];
    quantity_to_string_fixed(rem_q, 0, buf, sizeof(buf));
    assert(strcmp(buf, "900") == 0);
    money_to_string(rem_c, buf, sizeof(buf));
    assert(strcmp(buf, "10204.80") == 0);
    money_to_string(cost_red, buf, sizeof(buf));
    assert(strcmp(buf, "6803.20") == 0);
    money_to_string(realized_pnl, buf, sizeof(buf));
    assert(strcmp(buf, "2186.80") == 0);

    // Full Sell remaining 900 shares @ $16.00 with $5.00 fee
    // Sell Gross = 900 * 16 = 14400.00, Sell Net = 14395.00
    // Cost Reduction = 10204.80 (entire remaining cost)
    // Remaining Quantity = 0, Remaining Cost = 0.00
    // Realized PnL = 14395.00 - 10204.80 = +4190.20
    quantity_t sell2_q;
    quantity_from_string("900", &sell2_q);
    price_t sell2_p;
    price_from_string("16.00", CURRENCY_USD, &sell2_p);
    money_t sell2_fee;
    money_from_string("5.00", CURRENCY_USD, &sell2_fee);

    rc = ledger_calc_sell_position(rem_q, rem_c, sell2_q, sell2_p, sell2_fee,
                                   &rem_q, &rem_c, &cost_red, &realized_pnl);
    assert(rc == 0);
    assert(quantity_is_zero(rem_q));
    assert(money_is_zero(rem_c));
    money_to_string(realized_pnl, buf, sizeof(buf));
    assert(strcmp(buf, "4190.20") == 0);

    // Oversell attempt (trying to sell 1 share when balance is 0)
    quantity_t bad_q;
    quantity_from_string("1", &bad_q);
    rc = ledger_calc_sell_position(rem_q, rem_c, bad_q, sell2_p, sell2_fee,
                                   &rem_q, &rem_c, &cost_red, &realized_pnl);
    assert(rc < 0); // Must be rejected!
    printf("    ✓ Sell position math & oversell rejection passed.\n");
}

static void
test_unrealized_pnl(void)
{
    printf("  Testing Unrealized PnL Math...\n");
    // 500 shares @ $10.00 cost basis ($5000.00). Current market price = $12.50 ($6250.00)
    quantity_t q;
    quantity_from_string("500", &q);
    price_t p;
    price_from_string("12.50", CURRENCY_CNY, &p);
    money_t cost;
    money_from_string("5000.00", CURRENCY_CNY, &cost);

    money_t un_pnl;
    int rc = ledger_calc_unrealized_pnl(q, p, cost, &un_pnl);
    assert(rc == 0);
    char buf[64];
    money_to_string(un_pnl, buf, sizeof(buf));
    assert(strcmp(buf, "1250.00") == 0);
    printf("    ✓ Unrealized PnL math passed.\n");
}

int main(void)
{
    printf("=== Running Unit Test: Ledger Math Operators ===\n");
    test_buy_math();
    test_sell_math();
    test_unrealized_pnl();
    printf("🎉 ALL LEDGER MATH TESTS PASSED!\n");
    return 0;
}
