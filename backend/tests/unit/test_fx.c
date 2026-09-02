#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "core/financial/money.h"
#include "core/financial/percentage.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include "core/financial/rate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    printf("=== Running Unit Test: Dual-Factor FX PnL Decomposition ===\n");

    /*
     * Asset: US Stock AAPL in USD.
     * Reporting Base Currency: CNY.
     *
     * Buy: 100 shares @ $150.00 USD. Rate at cost = 7.0000 CNY/USD.
     * Current: 100 shares @ $180.00 USD. Current Rate = 7.2000 CNY/USD.
     */
    quantity_t shares;
    price_t    cost_price, curr_price;
    rate_t     cost_rate, curr_rate;
    assert(quantity_from_string("100", &shares) == DECIMAL_OK);
    assert(price_from_string("150.00", CURRENCY_USD, &cost_price) == DECIMAL_OK);
    assert(price_from_string("180.00", CURRENCY_USD, &curr_price) == DECIMAL_OK);
    assert(rate_from_string("7.0000", CURRENCY_USD, CURRENCY_CNY, &cost_rate) == DECIMAL_OK);
    assert(rate_from_string("7.2000", CURRENCY_USD, CURRENCY_CNY, &curr_rate) == DECIMAL_OK);

    /* 1. Original USD cost & Base CNY cost */
    money_t cost_usd, cost_cny;
    assert(price_times_quantity(cost_price, shares, &cost_usd) == DECIMAL_OK);
    assert(rate_convert_money(cost_usd, cost_rate, &cost_cny) == DECIMAL_OK);

    char buf[64];
    money_to_string(cost_usd, buf, sizeof(buf));
    assert(strcmp(buf, "15000.00") == 0);
    money_to_string(cost_cny, buf, sizeof(buf));
    assert(strcmp(buf, "105000.00") == 0);

    /* 2. Current USD value & Base CNY value */
    money_t curr_usd, curr_cny;
    assert(price_times_quantity(curr_price, shares, &curr_usd) == DECIMAL_OK);
    assert(rate_convert_money(curr_usd, curr_rate, &curr_cny) == DECIMAL_OK);

    money_to_string(curr_usd, buf, sizeof(buf));
    assert(strcmp(buf, "18000.00") == 0);
    money_to_string(curr_cny, buf, sizeof(buf));
    assert(strcmp(buf, "129600.00") == 0);

    /* 3. Combined Total PnL in Base CNY: 129600 - 105000 = 24600 CNY */
    money_t total_pnl_cny;
    assert(money_sub(curr_cny, cost_cny, &total_pnl_cny) == DECIMAL_OK);
    money_to_string(total_pnl_cny, buf, sizeof(buf));
    assert(strcmp(buf, "24600.00") == 0);

    /*
     * 4. Factor 1: Asset Price Gain in Base CNY
     * (Curr_USD - Cost_USD) * Curr_Rate = ($18000 - $15000) * 7.2000 = $3000 * 7.2000 = 21600.00 CNY
     */
    money_t asset_gain_usd, asset_pnl_cny;
    assert(money_sub(curr_usd, cost_usd, &asset_gain_usd) == DECIMAL_OK);
    assert(rate_convert_money(asset_gain_usd, curr_rate, &asset_pnl_cny) == DECIMAL_OK);
    money_to_string(asset_pnl_cny, buf, sizeof(buf));
    assert(strcmp(buf, "21600.00") == 0);

    /*
     * 5. Factor 2: Currency FX Gain in Base CNY
     * Cost_USD * (Curr_Rate - Cost_Rate) = $15000 * (7.2000 - 7.0000) = $15000 * 0.2000 = 3000.00 CNY
     */
    rate_t rate_diff;
    rate_diff.from_currency = CURRENCY_USD;
    rate_diff.to_currency = CURRENCY_CNY;
    assert(decimal_sub(curr_rate.factor, cost_rate.factor, &rate_diff.factor) == DECIMAL_OK);

    money_t fx_pnl_cny;
    assert(rate_convert_money(cost_usd, rate_diff, &fx_pnl_cny) == DECIMAL_OK);
    money_to_string(fx_pnl_cny, buf, sizeof(buf));
    assert(strcmp(buf, "3000.00") == 0);

    /*
     * 6. Exact Identity Verification:
     * Total PnL (24600.00) == Asset PnL (21600.00) + FX PnL (3000.00)
     */
    money_t decomposed_sum;
    assert(money_add(asset_pnl_cny, fx_pnl_cny, &decomposed_sum) == DECIMAL_OK);
    assert(money_cmp(decomposed_sum, total_pnl_cny) == 0);

    printf("✓ Dual-factor FX PnL mathematical identity verified with 100%% precision!\n");
    return 0;
}
