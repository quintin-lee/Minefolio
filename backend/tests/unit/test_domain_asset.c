#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "domain/asset/entity.h"
#include "domain/asset/rules.h"
#include "core/financial/currency.h"

static void test_asset_entity_and_predicates(void) {
    mf_asset_t a1 = {0};
    a1.id = 1;
    a1.user_id = 10;
    a1.category_id = 5;
    snprintf(a1.name, sizeof(a1.name), "贵州茅台");
    snprintf(a1.asset_type, sizeof(a1.asset_type), "stock");

    assert(mf_asset_is_investment(&a1) == true);
    assert(mf_asset_is_liability(&a1) == false);

    mf_asset_t a2 = {0};
    snprintf(a2.asset_type, sizeof(a2.asset_type), "credit_card");
    assert(mf_asset_is_investment(&a2) == false);
    assert(mf_asset_is_liability(&a2) == true);

    char err[256] = {0};
    int rc = mf_asset_rule_validate(&a1, err, sizeof(err));
    assert(rc == 0);

    printf("PASS: test_asset_entity_and_predicates\n");
}

static void test_asset_investment_derivation_and_pnl(void) {
    mf_asset_t asset = {0};
    asset.id = 10;
    asset.user_id = 1;
    asset.category_id = 2;
    snprintf(asset.asset_type, sizeof(asset.asset_type), "fund");
    currency_t cny = currency_from_str("CNY");
    asset.currency = cny;

    quantity_from_double(500.0, 4, &asset.quantity);
    price_from_double(2.50, 4, cny, &asset.net_value);

    /* 自动推导当前市值与成本基础 */
    int rc = mf_asset_rule_derive_investment_values(&asset);
    assert(rc == 0);
    assert(money_to_double(asset.current_value) == 1250.0);
    assert(money_to_double(asset.cost_basis) == 1250.0);

    /* 更新净值到 3.00，重算市值后计算浮动盈亏 */
    price_from_double(3.00, 4, cny, &asset.net_value);
    rc = mf_asset_rule_derive_investment_values(&asset);
    assert(rc == 0);
    assert(money_to_double(asset.current_value) == 1500.0);

    money_t pnl = {0};
    double pct = 0.0;
    rc = mf_asset_rule_calculate_floating_pnl(&asset, &pnl, &pct);
    assert(rc == 0);
    assert(money_to_double(pnl) == 250.0);
    assert(pct == 20.0);

    printf("PASS: test_asset_investment_derivation_and_pnl\n");
}

int main(void) {
    test_asset_entity_and_predicates();
    test_asset_investment_derivation_and_pnl();
    printf("All domain asset tests passed successfully!\n");
    return 0;
}
