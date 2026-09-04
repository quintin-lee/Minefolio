#include "domain/portfolio/portfolio.h"
#include <stdlib.h>
#include <string.h>

void
mf_portfolio_free(mf_portfolio_t* portfolio)
{
    if (!portfolio) {
        return;
    }
    if (portfolio->items) {
        free(portfolio->items);
        portfolio->items = NULL;
    }
    portfolio->item_count = 0;
}

int
mf_portfolio_aggregate(const mf_position_t*      positions,
                       size_t                    pos_count,
                       currency_t                reporting_currency,
                       const mf_fx_rate_table_t* fx_table,
                       mf_portfolio_t*           out_portfolio)
{
    if (!out_portfolio) {
        return -1;
    }
    memset(out_portfolio, 0, sizeof(*out_portfolio));
    out_portfolio->reporting_currency = reporting_currency;
    out_portfolio->total_market_value = money_zero(reporting_currency);
    out_portfolio->total_cost_basis = money_zero(reporting_currency);
    out_portfolio->total_realized_pnl = money_zero(reporting_currency);
    out_portfolio->total_unrealized_pnl = money_zero(reporting_currency);
    out_portfolio->total_pnl = money_zero(reporting_currency);

    if (!positions || pos_count == 0) {
        return 0;
    }

    out_portfolio->items = (mf_portfolio_item_t*)calloc(pos_count, sizeof(mf_portfolio_item_t));
    if (!out_portfolio->items) {
        return -1;
    }
    out_portfolio->item_count = pos_count;

    double tot_mv = 0.0;
    double tot_cost = 0.0;
    double tot_unrealized = 0.0;
    double tot_realized = 0.0;

    for (size_t i = 0; i < pos_count; i++) {
        const mf_position_t* pos = &positions[i];
        mf_portfolio_item_t* item = &out_portfolio->items[i];
        item->asset_id = pos->asset_id;
        item->native_currency = pos->currency;
        item->native_market_value = pos->valuation.market_value;

        // Convert market value
        if (mf_fx_convert_money(pos->valuation.market_value,
                                reporting_currency,
                                fx_table,
                                &item->converted_market_value) != 0) {
            mf_portfolio_free(out_portfolio);
            return -1; // Missing FX rate -> strictly reject
        }

        // Convert cost basis
        if (mf_fx_convert_money(pos->cost_basis.total_cost,
                                reporting_currency,
                                fx_table,
                                &item->converted_cost_basis) != 0) {
            mf_portfolio_free(out_portfolio);
            return -1;
        }

        // Convert unrealized pnl
        if (mf_fx_convert_money(pos->pnl.unrealized_pnl,
                                reporting_currency,
                                fx_table,
                                &item->converted_unrealized_pnl) != 0) {
            mf_portfolio_free(out_portfolio);
            return -1;
        }

        // Convert realized pnl
        if (mf_fx_convert_money(pos->pnl.realized_pnl,
                                reporting_currency,
                                fx_table,
                                &item->converted_realized_pnl) != 0) {
            mf_portfolio_free(out_portfolio);
            return -1;
        }

        tot_mv += money_to_double(item->converted_market_value);
        tot_cost += money_to_double(item->converted_cost_basis);
        tot_unrealized += money_to_double(item->converted_unrealized_pnl);
        tot_realized += money_to_double(item->converted_realized_pnl);
    }

    money_from_double(tot_mv, reporting_currency, &out_portfolio->total_market_value);
    money_from_double(tot_cost, reporting_currency, &out_portfolio->total_cost_basis);
    money_from_double(tot_unrealized, reporting_currency, &out_portfolio->total_unrealized_pnl);
    money_from_double(tot_realized, reporting_currency, &out_portfolio->total_realized_pnl);

    double tot_pnl = tot_realized + tot_unrealized;
    money_from_double(tot_pnl, reporting_currency, &out_portfolio->total_pnl);

    out_portfolio->unrealized_pct = (tot_cost != 0.0) ? (tot_unrealized / tot_cost) * 100.0 : 0.0;
    out_portfolio->total_return_pct = (tot_cost != 0.0) ? (tot_pnl / tot_cost) * 100.0 : 0.0;

    // Calculate weights & risk metrics (Concentration: max_holding_weight and Herfindahl index HHI)
    double  max_weight = 0.0;
    int64_t max_id = 0;
    double  hhi = 0.0;

    for (size_t i = 0; i < pos_count; i++) {
        mf_portfolio_item_t* item = &out_portfolio->items[i];
        double               it_mv = money_to_double(item->converted_market_value);
        item->weight = (tot_mv > 0.0) ? (it_mv / tot_mv) : 0.0;

        if (item->weight > max_weight) {
            max_weight = item->weight;
            max_id = item->asset_id;
        }
        hhi += item->weight * item->weight;
    }

    out_portfolio->max_holding_weight = max_weight;
    out_portfolio->max_holding_asset_id = max_id;
    out_portfolio->herfindahl_index = hhi;

    return 0;
}
