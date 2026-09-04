#include "domain/asset/position.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static inline quantity_t q_make(double d) {
    quantity_t q;
    quantity_from_double(d, 4, &q);
    return q;
}

static inline money_t m_make(double d, currency_t cur) {
    money_t m;
    money_from_double(d, cur, &m);
    return m;
}

static inline price_t p_make(double d, currency_t cur) {
    price_t p;
    price_from_double(d, 4, cur, &p);
    return p;
}

void test_position_derive_from_ledger_success() {
    int64_t asset_id = 101;
    int64_t account_id = 202;
    currency_t cny = CURRENCY_CNY;

    ledger_tx_t txs[3];
    memset(txs, 0, sizeof(txs));

    // 1. Buy 100 shares @ 10, amount 1000
    txs[0].asset_id = asset_id;
    txs[0].type = LEDGER_TX_BUY;
    txs[0].quantity = q_make(100);
    txs[0].amount = m_make(1000, cny);
    txs[0].fee = money_zero(cny);

    // 2. Sell 40 shares @ 15, amount 600
    txs[1].asset_id = asset_id;
    txs[1].type = LEDGER_TX_SELL;
    txs[1].quantity = q_make(40);
    txs[1].amount = m_make(600, cny);
    txs[1].fee = money_zero(cny);

    // 3. Dividend 50
    txs[2].asset_id = asset_id;
    txs[2].type = LEDGER_TX_DIVIDEND;
    txs[2].amount = m_make(50, cny);

    price_t cur_price = p_make(18, cny);
    mf_position_t pos;
    assert(mf_position_derive_from_ledger(asset_id, account_id, cny, txs, 3, cur_price, &pos) == 0);

    assert(pos.asset_id == asset_id);
    assert(pos.account_id == account_id);
    assert(currency_equals(pos.currency, cny));
    assert(quantity_to_double(pos.quantity) == 60.0);
    assert(money_to_double(pos.cost_basis.total_cost) == 600.0);
    assert(money_to_double(pos.valuation.market_value) == 1080.0);
    assert(money_to_double(pos.pnl.unrealized_pnl) == 480.0);
    assert(money_to_double(pos.pnl.realized_pnl) == 250.0);
    assert(money_to_double(pos.pnl.total_pnl) == 730.0);

    printf("PASS: test_position_derive_from_ledger_success\n");
}

void test_position_derive_oversell_fails() {
    int64_t asset_id = 101;
    int64_t account_id = 202;
    currency_t cny = CURRENCY_CNY;

    ledger_tx_t txs[2];
    memset(txs, 0, sizeof(txs));

    // Buy 50
    txs[0].asset_id = asset_id;
    txs[0].type = LEDGER_TX_BUY;
    txs[0].quantity = q_make(50);
    txs[0].amount = m_make(500, cny);

    // Try to Sell 60
    txs[1].asset_id = asset_id;
    txs[1].type = LEDGER_TX_SELL;
    txs[1].quantity = q_make(60);
    txs[1].amount = m_make(600, cny);

    mf_position_t pos;
    assert(mf_position_derive_from_ledger(asset_id, account_id, cny, txs, 2, p_make(10, cny), &pos) == -1);

    printf("PASS: test_position_derive_oversell_fails\n");
}

int main() {
    test_position_derive_from_ledger_success();
    test_position_derive_oversell_fails();
    printf("All domain position tests passed!\n");
    return 0;
}
