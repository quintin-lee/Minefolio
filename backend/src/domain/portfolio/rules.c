#include "domain/portfolio/rules.h"
#include "core/financial/percentage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t    asset_id;
    money_t    cost_for_pnl;
    quantity_t qty;
    money_t    realized;
} pnl_acc_t;

int
mf_portfolio_rule_apply_trade_events(mf_holding_item_t*                items,
                                     size_t                            item_count,
                                     const mf_portfolio_trade_event_t* events,
                                     size_t                            event_count)
{
    if (!items && item_count > 0) {
        return -1;
    }
    if (item_count == 0) {
        return 0;
    }

    pnl_acc_t* accs = (pnl_acc_t*)calloc(item_count, sizeof(pnl_acc_t));
    if (!accs) {
        return -1;
    }

    for (size_t i = 0; i < item_count; i++) {
        accs[i].asset_id = items[i].asset_id;
        accs[i].cost_for_pnl = money_zero(items[i].currency);
        accs[i].qty = quantity_zero();
        accs[i].realized = money_zero(items[i].currency);
    }

    if (events && event_count > 0) {
        for (size_t i = 0; i < event_count; i++) {
            const mf_portfolio_trade_event_t* ev = &events[i];
            if (!ev->type[0]) {
                continue;
            }
            if (strcmp(ev->type, "buy") != 0 && strcmp(ev->type, "sell") != 0 &&
                strcmp(ev->type, "income") != 0) {
                continue;
            }

            int found_idx = -1;
            for (size_t j = 0; j < item_count; j++) {
                if (accs[j].asset_id == ev->asset_id) {
                    found_idx = (int)j;
                    break;
                }
            }
            if (found_idx < 0) {
                continue;
            }

            pnl_acc_t* acc = &accs[found_idx];

            if (strcmp(ev->type, "buy") == 0) {
                money_add(acc->cost_for_pnl, ev->amount, &acc->cost_for_pnl);
                quantity_add(acc->qty, ev->quantity, &acc->qty);
            } else if (strcmp(ev->type, "sell") == 0) {
                if (quantity_is_positive(acc->qty)) {
                    money_t sold_cost = money_zero(acc->cost_for_pnl.currency);
                    if (quantity_cmp(ev->quantity, acc->qty) >= 0) {
                        sold_cost = acc->cost_for_pnl;
                    } else {
                        decimal_t ratio;
                        if (decimal_div(
                                ev->quantity.units, acc->qty.units, 12, ROUND_HALF_UP, &ratio) ==
                            DECIMAL_OK) {
                            decimal_t sold_cost_units;
                            if (decimal_mul(acc->cost_for_pnl.amount, ratio, &sold_cost_units) ==
                                DECIMAL_OK) {
                                decimal_round(sold_cost_units,
                                              currency_precision(acc->cost_for_pnl.currency),
                                              ROUND_HALF_UP,
                                              &sold_cost.amount);
                            }
                        }
                    }
                    money_t trade_realized;
                    money_sub(ev->amount, sold_cost, &trade_realized);
                    money_add(acc->realized, trade_realized, &acc->realized);
                    quantity_sub(acc->qty, ev->quantity, &acc->qty);
                }
            } else if (strcmp(ev->type, "income") == 0) {
                money_sub(acc->cost_for_pnl, ev->amount, &acc->cost_for_pnl);
                money_add(acc->realized, ev->amount, &acc->realized);
            }
        }
    }

    for (size_t i = 0; i < item_count; i++) {
        mf_holding_item_t* it = &items[i];
        money_t            market = {0};
        if (price_times_quantity(it->net_value, it->quantity, &market) == DECIMAL_OK) {
            it->market_value = market;
        } else {
            it->market_value = money_zero(it->currency);
        }

        money_sub(it->market_value, it->cost_basis, &it->floating_pnl);
        percentage_t pct;
        if (percentage_calc(it->floating_pnl, it->cost_basis, 4, ROUND_HALF_UP, &pct) ==
            DECIMAL_OK) {
            it->floating_pct = percentage_to_double(pct);
        } else {
            it->floating_pct = 0.0;
        }
        it->realized_pnl = accs[i].realized;
    }

    free(accs);
    return 0;
}

int
mf_portfolio_rule_aggregate_summary(const mf_holding_item_t* items,
                                    size_t                   item_count,
                                    currency_t               base_currency,
                                    mf_portfolio_summary_t*  out_summary)
{
    if (!out_summary) {
        return -1;
    }
    memset(out_summary, 0, sizeof(*out_summary));

    out_summary->total_market_value = money_zero(base_currency);
    out_summary->total_cost_basis = money_zero(base_currency);
    out_summary->total_floating_pnl = money_zero(base_currency);
    out_summary->total_realized_pnl = money_zero(base_currency);
    out_summary->floating_pct = 0.0;

    if (!items || item_count == 0) {
        return 0;
    }

    for (size_t i = 0; i < item_count; i++) {
        money_add(out_summary->total_market_value,
                  items[i].market_value,
                  &out_summary->total_market_value);
        money_add(
            out_summary->total_cost_basis, items[i].cost_basis, &out_summary->total_cost_basis);
        money_add(out_summary->total_floating_pnl,
                  items[i].floating_pnl,
                  &out_summary->total_floating_pnl);
        money_add(out_summary->total_realized_pnl,
                  items[i].realized_pnl,
                  &out_summary->total_realized_pnl);
    }

    percentage_t pct;
    if (percentage_calc(out_summary->total_floating_pnl,
                        out_summary->total_cost_basis,
                        4,
                        ROUND_HALF_UP,
                        &pct) == DECIMAL_OK) {
        out_summary->floating_pct = percentage_to_double(pct);
    } else {
        out_summary->floating_pct = 0.0;
    }

    return 0;
}
