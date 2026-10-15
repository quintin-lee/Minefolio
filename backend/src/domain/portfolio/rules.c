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
                strcmp(ev->type, "income") != 0 && strcmp(ev->type, "dividend") != 0 &&
                strcmp(ev->type, "interest") != 0 && strcmp(ev->type, "fee") != 0) {
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
                money_t buy_cost = ev->amount;
                money_add(buy_cost, ev->fee, &buy_cost);
                money_add(acc->cost_for_pnl, buy_cost, &acc->cost_for_pnl);
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
                    money_t proceeds = ev->amount;
                    money_sub(proceeds, ev->fee, &proceeds);
                    money_t trade_realized;
                    money_sub(proceeds, sold_cost, &trade_realized);
                    money_add(acc->realized, trade_realized, &acc->realized);

                    /* 扣减份额与成本池（清仓时直接置零，彻底消除浮点微小残差） */
                    if (quantity_cmp(ev->quantity, acc->qty) >= 0) {
                        acc->qty = quantity_zero();
                        acc->cost_for_pnl = money_zero(acc->cost_for_pnl.currency);
                    } else {
                        quantity_sub(acc->qty, ev->quantity, &acc->qty);
                        money_sub(acc->cost_for_pnl, sold_cost, &acc->cost_for_pnl);
                    }
                }
            } else if (strcmp(ev->type, "income") == 0 || strcmp(ev->type, "dividend") == 0 ||
                       strcmp(ev->type, "interest") == 0) {
                money_t net_inc = ev->amount;
                money_sub(net_inc, ev->fee, &net_inc);
                money_add(acc->realized, net_inc, &acc->realized);
            } else if (strcmp(ev->type, "fee") == 0) {
                money_t fee_amt = money_is_positive(ev->fee) ? ev->fee : ev->amount;
                money_sub(acc->realized, fee_amt, &acc->realized);
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

        if (quantity_is_zero(it->quantity)) {
            it->market_value = money_zero(it->currency);
            it->floating_pnl = money_zero(it->currency);
            it->floating_pct = 0.0;
        } else {
            money_sub(it->market_value, it->cost_basis, &it->floating_pnl);
            percentage_t pct;
            if (percentage_calc(it->floating_pnl, it->cost_basis, 4, ROUND_HALF_UP, &pct) ==
                DECIMAL_OK) {
                it->floating_pct = percentage_to_double(pct);
            } else {
                it->floating_pct = 0.0;
            }
        }
        it->realized_pnl = accs[i].realized;
    }

    free(accs);
    return 0;
}

int
mf_portfolio_rule_aggregate_summary(const mf_holding_item_t*  items,
                                    size_t                    item_count,
                                    currency_t                base_currency,
                                    const mf_fx_rate_table_t* fx_table,
                                    mf_portfolio_summary_t*   out_summary)
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
        money_t conv_market = items[i].market_value;
        money_t conv_cost = items[i].cost_basis;
        money_t conv_floating = items[i].floating_pnl;
        money_t conv_realized = items[i].realized_pnl;

        if (!currency_equals(items[i].currency, base_currency)) {
            if (!fx_table) {
                // 币种不一致且无汇率表时严禁盲目相加
                return -1;
            }
            if (mf_fx_convert_money(items[i].market_value, base_currency, fx_table, &conv_market) !=
                    0 ||
                mf_fx_convert_money(items[i].cost_basis, base_currency, fx_table, &conv_cost) !=
                    0 ||
                mf_fx_convert_money(
                    items[i].floating_pnl, base_currency, fx_table, &conv_floating) != 0 ||
                mf_fx_convert_money(
                    items[i].realized_pnl, base_currency, fx_table, &conv_realized) != 0) {
                return -1;
            }
        }

        money_add(out_summary->total_market_value, conv_market, &out_summary->total_market_value);
        money_add(out_summary->total_cost_basis, conv_cost, &out_summary->total_cost_basis);
        money_add(out_summary->total_floating_pnl, conv_floating, &out_summary->total_floating_pnl);
        money_add(out_summary->total_realized_pnl, conv_realized, &out_summary->total_realized_pnl);
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
