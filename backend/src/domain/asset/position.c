#include "domain/asset/position.h"
#include <string.h>

int
mf_position_derive_from_ledger(int64_t            asset_id,
                               int64_t            account_id,
                               currency_t         native_currency,
                               const ledger_tx_t* tx_events,
                               size_t             tx_count,
                               price_t            current_price,
                               mf_position_t*     out_position)
{
    if (!out_position) {
        return -1;
    }
    memset(out_position, 0, sizeof(*out_position));
    out_position->asset_id = asset_id;
    out_position->account_id = account_id;
    out_position->currency = native_currency;
    out_position->cost_basis = mf_cost_basis_init(native_currency);

    money_t accum_realized = money_zero(native_currency);

    if (tx_events && tx_count > 0) {
        for (size_t i = 0; i < tx_count; i++) {
            const ledger_tx_t* tx = &tx_events[i];
            if (tx->asset_id != asset_id) {
                continue;
            }

            if (tx->type == LEDGER_TX_BUY) {
                if (mf_cost_basis_apply_buy(
                        &out_position->cost_basis, tx->quantity, tx->amount, tx->fee) != 0) {
                    return -1;
                }
            } else if (tx->type == LEDGER_TX_SELL) {
                if (mf_cost_basis_apply_sell(&out_position->cost_basis,
                                             &accum_realized,
                                             tx->quantity,
                                             tx->amount,
                                             tx->fee) != 0) {
                    return -1; // Oversell or error
                }
            } else if (tx->type == LEDGER_TX_DIVIDEND) {
                if (mf_cost_basis_apply_dividend(
                        &out_position->cost_basis, &accum_realized, tx->amount) != 0) {
                    return -1;
                }
            }
        }
    }

    out_position->quantity = out_position->cost_basis.quantity;
    out_position->valuation =
        mf_valuation_calculate(out_position->quantity, current_price, NULL, "market");
    out_position->pnl = mf_pnl_calculate(
        out_position->cost_basis.total_cost, out_position->valuation.market_value, accum_realized);
    return 0;
}
