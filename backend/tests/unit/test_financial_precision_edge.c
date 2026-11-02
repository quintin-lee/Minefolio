#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include "core/financial/money.h"
#include "domain/market/rules.h"

int main(void) {
    printf("Running test_financial_precision_edge...\n");

    /* 1. Test precision multiplication: 1000.0000 shares @ 19.99 CNY */
    quantity_t qty;
    assert(quantity_from_double(1000.0, 4, &qty) == DECIMAL_OK);

    price_t price;
    assert(price_from_double(19.99, 4, CURRENCY_CNY, &price) == DECIMAL_OK);

    money_t total_val;
    assert(price_times_quantity(price, qty, &total_val) == DECIMAL_OK);

    /* 2. Verify exact balance without float residual */
    char val_str[64];
    money_to_string(total_val, val_str, sizeof(val_str));
    assert(strcmp(val_str, "19990.00") == 0);

    /* 3. Test exact delta subtraction with 3-arg money_sub */
    money_t old_val;
    assert(money_from_double(19900.00, CURRENCY_CNY, &old_val) == DECIMAL_OK);

    money_t delta;
    assert(money_sub(total_val, old_val, &delta) == DECIMAL_OK);

    char delta_str[64];
    money_to_string(delta, delta_str, sizeof(delta_str));
    assert(strcmp(delta_str, "90.00") == 0);

    /* 4. Directly test mf_market_rule_calc_sync_delta calculation */
    price_t p_old;
    assert(price_from_double(0.0, 4, CURRENCY_CNY, &p_old) == DECIMAL_OK);
    price_t p_new = price;
    money_t sync_delta;
    mf_market_rule_calc_sync_delta(p_old, p_new, qty, CURRENCY_CNY, &sync_delta);
    char sync_delta_str[64];
    money_to_string(sync_delta, sync_delta_str, sizeof(sync_delta_str));
    assert(strcmp(sync_delta_str, "19990.00") == 0);

    printf("PASS: test_financial_precision_edge completed successfully\n");
    return 0;
}
