#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "domain/transaction/entity.h"
#include "domain/transaction/rules.h"
#include "core/financial/currency.h"

static void test_transaction_entity_and_predicates(void) {
    mf_transaction_t tx = {0};
    tx.id = 101;
    tx.user_id = 1;
    tx.asset_id = 10;
    tx.account_id = 20;

    currency_t cny = currency_from_str("CNY");
    quantity_from_double(5.0, 4, &tx.amount);
    price_from_double(150.25, 4, cny, &tx.price);
    money_from_double(12.50, cny, &tx.fee);
    snprintf(tx.fee_currency, sizeof(tx.fee_currency), "CNY");
    snprintf(tx.type, sizeof(tx.type), "buy");
    snprintf(tx.note, sizeof(tx.note), "加仓测试");

    assert(mf_tx_is_investment(&tx) == true);
    assert(mf_tx_has_fee(&tx) == true);
    assert(mf_tx_is_fee_child(&tx) == false);

    /* 校验规则通过 */
    char err[256] = {0};
    int rc = mf_tx_rule_validate(&tx, err, sizeof(err));
    assert(rc == 0);
    assert(err[0] == '\0');

    /* 构建手续费子单不变式验证 */
    mf_transaction_t fee_child = {0};
    rc = mf_tx_rule_build_fee_child(&tx, &fee_child);
    assert(rc == 0);
    assert(fee_child.parent_tx_id == 101);
    assert(fee_child.user_id == 1);
    assert(fee_child.asset_id == 0);
    assert(fee_child.account_id == 20);
    assert(strcmp(fee_child.type, "fee") == 0);
    assert(strstr(fee_child.note, "fee") != NULL);
    assert(mf_tx_is_fee_child(&fee_child) == true);
    assert(mf_tx_has_fee(&fee_child) == false);

    printf("PASS: test_transaction_entity_and_predicates\n");
}

static void test_transaction_validation_failures(void) {
    char err[256] = {0};

    /* 空指针校验 */
    int rc = mf_tx_rule_validate(NULL, err, sizeof(err));
    assert(rc != 0);

    /* 用户 ID 无效 */
    mf_transaction_t tx = {0};
    rc = mf_tx_rule_validate(&tx, err, sizeof(err));
    assert(rc != 0);

    /* 投资买入缺少 asset_id */
    tx.user_id = 1;
    currency_t cny = currency_from_str("CNY");
    snprintf(tx.type, sizeof(tx.type), "buy");
    price_from_double(10.0, 4, cny, &tx.price);
    quantity_from_double(1.0, 4, &tx.amount);
    rc = mf_tx_rule_validate(&tx, err, sizeof(err));
    assert(rc != 0);

    /* 投资买入缺少价格 */
    tx.asset_id = 5;
    tx.price = price_zero(cny);
    rc = mf_tx_rule_validate(&tx, err, sizeof(err));
    assert(rc != 0);

    /* 恢复正常买入 */
    price_from_double(10.0, 4, cny, &tx.price);
    rc = mf_tx_rule_validate(&tx, err, sizeof(err));
    assert(rc == 0);

    /* 负数金额校验 */
    tx.amount = quantity_neg(tx.amount);
    rc = mf_tx_rule_validate(&tx, err, sizeof(err));
    assert(rc != 0);

    printf("PASS: test_transaction_validation_failures\n");
}

int main(void) {
    test_transaction_entity_and_predicates();
    test_transaction_validation_failures();
    printf("All domain transaction tests passed successfully!\n");
    return 0;
}
