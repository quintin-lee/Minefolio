#include "domain/portfolio/rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t asset_id;
    double  cost_for_pnl;
    double  qty;
    double  realized;
} pnl_acc_t;

int mf_portfolio_rule_apply_trade_events(mf_holding_item_t* items, size_t item_count,
                                        const mf_portfolio_trade_event_t* events, size_t event_count) {
    if (!items && item_count > 0) return -1;
    if (item_count == 0) return 0;

    pnl_acc_t* accs = (pnl_acc_t*)calloc(item_count, sizeof(pnl_acc_t));
    if (!accs) return -1;

    for (size_t i = 0; i < item_count; i++) {
        accs[i].asset_id = items[i].asset_id;
    }

    if (events && event_count > 0) {
        for (size_t i = 0; i < event_count; i++) {
            const mf_portfolio_trade_event_t* ev = &events[i];
            if (!ev->type[0]) continue;
            if (strcmp(ev->type, "buy") != 0 && strcmp(ev->type, "sell") != 0 && strcmp(ev->type, "income") != 0) {
                continue;
            }

            int found_idx = -1;
            for (size_t j = 0; j < item_count; j++) {
                if (accs[j].asset_id == ev->asset_id) {
                    found_idx = (int)j;
                    break;
                }
            }
            if (found_idx < 0) continue;

            double amt = money_to_double(ev->amount);
            double qty = quantity_to_double(ev->quantity);

            if (strcmp(ev->type, "buy") == 0) {
                accs[found_idx].cost_for_pnl += amt;
                accs[found_idx].qty += qty;
            } else if (strcmp(ev->type, "sell") == 0) {
                double avg_cost = accs[found_idx].qty > 0.0 ? (accs[found_idx].cost_for_pnl / accs[found_idx].qty) : 0.0;
                accs[found_idx].realized += amt - qty * avg_cost;
                accs[found_idx].qty -= qty;
            } else if (strcmp(ev->type, "income") == 0) {
                accs[found_idx].cost_for_pnl -= amt;
                accs[found_idx].realized += amt;
            }
        }
    }

    for (size_t i = 0; i < item_count; i++) {
        mf_holding_item_t* it = &items[i];
        money_t market = {0};
        if (price_times_quantity(it->net_value, it->quantity, &market) == DECIMAL_OK) {
            it->market_value = market;
        } else {
            it->market_value = money_zero(it->currency);
        }

        money_sub(it->market_value, it->cost_basis, &it->floating_pnl);
        double cost_d = money_to_double(it->cost_basis);
        double float_d = money_to_double(it->floating_pnl);
        it->floating_pct = (cost_d != 0.0) ? (float_d / cost_d) * 100.0 : 0.0;
        money_from_double(accs[i].realized, it->currency, &it->realized_pnl);
    }

    free(accs);
    return 0;
}

int mf_portfolio_rule_aggregate_summary(const mf_holding_item_t* items, size_t item_count,
                                       currency_t base_currency, mf_portfolio_summary_t* out_summary) {
    if (!out_summary) return -1;
    memset(out_summary, 0, sizeof(*out_summary));

    out_summary->total_market_value = money_zero(base_currency);
    out_summary->total_cost_basis = money_zero(base_currency);
    out_summary->total_floating_pnl = money_zero(base_currency);
    out_summary->total_realized_pnl = money_zero(base_currency);
    out_summary->floating_pct = 0.0;

    if (!items || item_count == 0) return 0;

    double tot_market = 0.0, tot_cost = 0.0, tot_floating = 0.0, tot_realized = 0.0;
    for (size_t i = 0; i < item_count; i++) {
        tot_market += money_to_double(items[i].market_value);
        tot_cost += money_to_double(items[i].cost_basis);
        tot_floating += money_to_double(items[i].floating_pnl);
        tot_realized += money_to_double(items[i].realized_pnl);
    }

    money_from_double(tot_market, base_currency, &out_summary->total_market_value);
    money_from_double(tot_cost, base_currency, &out_summary->total_cost_basis);
    money_from_double(tot_floating, base_currency, &out_summary->total_floating_pnl);
    money_from_double(tot_realized, base_currency, &out_summary->total_realized_pnl);
    out_summary->floating_pct = (tot_cost != 0.0) ? (tot_floating / tot_cost) * 100.0 : 0.0;

    return 0;
}
