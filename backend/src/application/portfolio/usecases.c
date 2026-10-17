#include "application/portfolio/usecases.h"
#include "domain/portfolio/repository.h"
#include "domain/portfolio/rules.h"
#include "infrastructure/repositories/portfolio_repo_impl.h"
#include "services/market/exchange_rate_service.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
portfolio_usecase_get_holdings(void*                        db_pool,
                               const query_portfolio_cmd_t* cmd,
                               csilk_json_t**               out_resp,
                               portfolio_usecase_result_t*  out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!cmd || !db_pool || !out_resp) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    mf_holding_item_t* items = NULL;
    size_t             item_count = 0;
    if (mf_portfolio_repo_get_holdings(
            db_pool, cmd->user_id, cmd->ledger_id, &items, &item_count) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询持仓标的失败");
        return -1;
    }

    mf_portfolio_trade_event_t* events = NULL;
    size_t                      event_count = 0;
    if (mf_portfolio_repo_get_trade_events(
            db_pool, cmd->user_id, cmd->ledger_id, &events, &event_count) != 0) {
        mf_portfolio_repo_free_holdings(items, item_count);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询交易事实流水失败");
        return -1;
    }

    /* 纯领域规则：重放交易计算持仓盈亏 */
    mf_portfolio_rule_apply_trade_events(items, item_count, events, event_count);

    /* 纯领域规则：聚合组合整体汇总（构建汇率表支持跨币种无损折算） */
    currency_t base_cur = currency_from_str(cmd->base_currency ? cmd->base_currency : "CNY");
    mf_fx_rate_table_t fx_table;
    mf_fx_rate_table_init(&fx_table);
    for (size_t i = 0; i < item_count; i++) {
        if (!currency_equals(items[i].currency, base_cur)) {
            rate_t r = exchange_rate_get_rate(items[i].currency, base_cur);
            mf_fx_rate_table_add(&fx_table, items[i].currency, base_cur, r);
        }
    }
    mf_portfolio_summary_t summary = {0};
    mf_portfolio_rule_aggregate_summary(items, item_count, base_cur, &fx_table, &summary);
    mf_fx_rate_table_free(&fx_table);

    /* 组装响应 DTO */
    csilk_json_t* holdings_arr = csilk_json_array();
    for (size_t i = 0; i < item_count; i++) {
        const mf_holding_item_t* it = &items[i];
        csilk_json_t*            h = csilk_json_object();
        csilk_json_add_number(h, "asset_id", (double)it->asset_id);
        csilk_json_add_string(h, "name", it->name);
        csilk_json_add_string(h, "asset_type", it->asset_type);
        csilk_json_add_string(h, "currency", currency_code(&it->currency));
        csilk_json_add_number(
            h, "quantity", mf_round_to_decimals(quantity_to_double(it->quantity), 4));
        csilk_json_add_number(
            h, "net_value", mf_round_to_decimals(price_to_double(it->net_value), 4));
        csilk_json_add_number(
            h, "cost_basis", mf_round_to_decimals(money_to_double(it->cost_basis), 2));
        csilk_json_add_number(
            h, "current_value", mf_round_to_decimals(money_to_double(it->market_value), 2));
        csilk_json_add_number(
            h, "floating_pnl", mf_round_to_decimals(money_to_double(it->floating_pnl), 2));
        csilk_json_add_number(h, "floating_pct", mf_round_to_decimals(it->floating_pct, 2));
        csilk_json_add_number(
            h, "realized_pnl", mf_round_to_decimals(money_to_double(it->realized_pnl), 2));
        csilk_json_array_append(holdings_arr, h);
    }

    csilk_json_t* summary_obj = csilk_json_object();
    csilk_json_add_number(summary_obj,
                          "total_market_value",
                          mf_round_to_decimals(money_to_double(summary.total_market_value), 2));
    csilk_json_add_number(summary_obj,
                          "total_cost_basis",
                          mf_round_to_decimals(money_to_double(summary.total_cost_basis), 2));
    csilk_json_add_number(summary_obj,
                          "total_floating_pnl",
                          mf_round_to_decimals(money_to_double(summary.total_floating_pnl), 2));
    csilk_json_add_number(summary_obj,
                          "total_realized_pnl",
                          mf_round_to_decimals(money_to_double(summary.total_realized_pnl), 2));
    csilk_json_add_number(
        summary_obj, "floating_pct", mf_round_to_decimals(summary.floating_pct, 2));

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_object(resp, "summary", summary_obj);
    csilk_json_add_array(resp, "holdings", holdings_arr);

    mf_portfolio_repo_free_trade_events(events, event_count);
    mf_portfolio_repo_free_holdings(items, item_count);

    *out_resp = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
portfolio_usecase_get_performance(void*                        db_pool,
                                  const query_portfolio_cmd_t* cmd,
                                  csilk_json_t**               out_resp,
                                  portfolio_usecase_result_t*  out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!cmd || !db_pool || !out_resp) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    csilk_json_t* result =
        portfolio_repo_get_performance_transactions(db_pool, cmd->user_id, cmd->ledger_id);
    if (!result) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询失败");
        return -1;
    }

    typedef struct {
        int64_t    asset_id;
        currency_t currency;
        quantity_t total_qty;
        money_t    cost_basis;
    } asset_perf_tracker_t;

    size_t                n = csilk_json_array_size(result);
    asset_perf_tracker_t* trackers =
        (asset_perf_tracker_t*)calloc(n > 0 ? n : 1, sizeof(asset_perf_tracker_t));
    if (!trackers) {
        csilk_json_free(result);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "内存分配失败");
        return -1;
    }
    size_t tracker_count = 0;

    currency_t    base_cur = currency_from_str(cmd->base_currency ? cmd->base_currency : "CNY");
    double        total_gain = 0, total_loss = 0;
    double        total_realized_pnl = 0;
    int           total_trades = 0;
    csilk_json_t* trades = csilk_json_array();

    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(result, i);
        int64_t       aid = db_get_int(row, "asset_id");
        const char*   type = csilk_json_get_string(row, "transaction_type");
        const char*   date_s = csilk_json_get_string(row, "transaction_date");
        const char*   cur_s = csilk_json_get_string(row, "currency");
        if (!cur_s || !cur_s[0]) {
            cur_s = "CNY";
        }
        currency_t cur = currency_from_str(cur_s);
        money_t    amt = db_get_money(row, "amount", cur);
        money_t    fee = db_get_money(row, "fee", cur);
        quantity_t qty = db_get_quantity(row, "quantity");
        price_t    price = db_get_price(row, "price_per_unit", cur);

        if (money_is_zero(amt) && !decimal_is_zero(price.unit_price) && !quantity_is_zero(qty)) {
            price_times_quantity(price, qty, &amt);
        }

        /* Lookup or allocate tracker for this specific asset */
        int trk_idx = -1;
        for (size_t k = 0; k < tracker_count; k++) {
            if (trackers[k].asset_id == aid) {
                trk_idx = (int)k;
                break;
            }
        }
        if (trk_idx < 0) {
            trk_idx = (int)(tracker_count++);
            trackers[trk_idx].asset_id = aid;
            trackers[trk_idx].currency = cur;
            trackers[trk_idx].total_qty = quantity_zero();
            trackers[trk_idx].cost_basis = money_zero(cur);
        }
        asset_perf_tracker_t* trk = &trackers[trk_idx];

        double  avg_cost = 0.0;
        money_t realized_m = money_zero(cur);

        if (type && strcmp(type, "buy") == 0 && quantity_is_positive(qty)) {
            total_trades++;
            money_t total_buy_cost;
            money_add(amt, fee, &total_buy_cost);
            quantity_add(trk->total_qty, qty, &trk->total_qty);
            money_add(trk->cost_basis, total_buy_cost, &trk->cost_basis);
            price_t avg_p;
            if (money_div_quantity(trk->cost_basis, trk->total_qty, 4, ROUND_HALF_UP, &avg_p) ==
                DECIMAL_OK) {
                avg_cost = price_to_double(avg_p);
            } else {
                avg_cost = price_to_double(price);
            }
            realized_m = money_zero(cur);
        } else if (type && strcmp(type, "sell") == 0 && quantity_is_positive(qty)) {
            total_trades++;
            price_t avg_p;
            if (quantity_is_positive(trk->total_qty) &&
                money_div_quantity(trk->cost_basis, trk->total_qty, 4, ROUND_HALF_UP, &avg_p) ==
                    DECIMAL_OK) {
                avg_cost = price_to_double(avg_p);
            } else {
                avg_cost = price_to_double(price);
            }

            money_t cost_reduction = money_zero(cur);
            if (quantity_is_positive(trk->total_qty)) {
                if (quantity_cmp(qty, trk->total_qty) >= 0) {
                    cost_reduction = trk->cost_basis;
                } else {
                    decimal_t ratio;
                    if (decimal_div(qty.units, trk->total_qty.units, 12, ROUND_HALF_UP, &ratio) ==
                        DECIMAL_OK) {
                        decimal_t sold_units;
                        if (decimal_mul(trk->cost_basis.amount, ratio, &sold_units) == DECIMAL_OK) {
                            decimal_round(sold_units,
                                          currency_precision(cur),
                                          ROUND_HALF_UP,
                                          &cost_reduction.amount);
                        }
                    }
                }
                money_t net_sell_proceeds;
                money_sub(amt, fee, &net_sell_proceeds);
                money_sub(net_sell_proceeds, cost_reduction, &realized_m);
                if (quantity_cmp(qty, trk->total_qty) >= 0) {
                    trk->total_qty = quantity_zero();
                    trk->cost_basis = money_zero(cur);
                } else {
                    quantity_sub(trk->total_qty, qty, &trk->total_qty);
                    money_sub(trk->cost_basis, cost_reduction, &trk->cost_basis);
                }
            } else {
                money_sub(amt, fee, &realized_m);
            }

            money_t conv_realized = exchange_rate_convert_m(realized_m, base_cur);
            double  conv_r = money_to_double(conv_realized);
            total_realized_pnl += conv_r;
            if (conv_r >= 0) {
                total_gain += conv_r;
            } else {
                total_loss += (-conv_r);
            }
        } else if (type && (strcmp(type, "income") == 0 || strcmp(type, "dividend") == 0)) {
            total_trades++;
            realized_m = amt;
            money_t conv_realized = exchange_rate_convert_m(realized_m, base_cur);
            double  conv_r = money_to_double(conv_realized);
            total_realized_pnl += conv_r;
            if (conv_r >= 0) {
                total_gain += conv_r;
            } else {
                total_loss += (-conv_r);
            }
            avg_cost = 0.0;
        } else if (type && strcmp(type, "fee") == 0) {
            total_trades++;
            money_t fee_amt = money_is_positive(amt) ? amt : fee;
            realized_m = money_neg(fee_amt);
            money_t conv_realized = exchange_rate_convert_m(realized_m, base_cur);
            double  conv_r = money_to_double(conv_realized);
            total_realized_pnl += conv_r;
            total_loss += (-conv_r);
            avg_cost = 0.0;
        }

        csilk_json_t* trade = csilk_json_object();
        csilk_json_add_number(trade, "id", db_get_num(row, "id"));
        csilk_json_add_string(trade, "asset_name", csilk_json_get_string(row, "asset_name"));
        csilk_json_add_string(trade, "type", type ? type : "");
        csilk_json_add_string(trade, "date", date_s ? date_s : "");
        csilk_json_add_number(trade, "quantity", mf_round_to_decimals(quantity_to_double(qty), 4));
        csilk_json_add_number(trade, "price", mf_round_to_decimals(price_to_double(price), 4));
        csilk_json_add_number(trade, "amount", mf_round_to_decimals(money_to_double(amt), 2));
        csilk_json_add_number(trade, "avg_cost_at_trade", mf_round_to_decimals(avg_cost, 4));
        csilk_json_add_number(
            trade, "realized", mf_round_to_decimals(money_to_double(realized_m), 2));
        csilk_json_add_number(trade, "fee", mf_round_to_decimals(money_to_double(fee), 2));
        csilk_json_array_append(trades, trade);
    }
    free(trackers);
    csilk_json_free(result);

    double market_value = 0, cost_basis_remaining = 0, dummy_qty = 0;
    portfolio_repo_get_current_holdings_totals(
        db_pool, cmd->user_id, cmd->ledger_id, &dummy_qty, &cost_basis_remaining, &market_value);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "total_trades", total_trades);
    csilk_json_add_number(resp, "total_gain", mf_round_to_decimals(total_gain, 2));
    csilk_json_add_number(resp, "total_loss", mf_round_to_decimals(total_loss, 2));
    csilk_json_add_number(resp, "net_gain", mf_round_to_decimals(total_gain - total_loss, 2));
    csilk_json_add_number(
        resp, "total_cost_basis_remaining", mf_round_to_decimals(cost_basis_remaining, 2));
    csilk_json_add_number(resp, "total_market_value", mf_round_to_decimals(market_value, 2));
    csilk_json_add_number(
        resp, "floating_pnl", mf_round_to_decimals(market_value - cost_basis_remaining, 2));
    csilk_json_add_number(resp, "realized_pnl", mf_round_to_decimals(total_realized_pnl, 2));
    csilk_json_add_array(resp, "trades", trades);

    *out_resp = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
portfolio_usecase_get_dashboard_summary(void*                        db_pool,
                                        const query_portfolio_cmd_t* cmd,
                                        csilk_json_t**               out_resp,
                                        portfolio_usecase_result_t*  out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!cmd || !db_pool || !out_resp) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    csilk_json_t* rows = portfolio_repo_get_category_assets(db_pool, cmd->user_id, cmd->ledger_id);
    if (!rows) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询失败");
        return -1;
    }

    typedef struct {
        char   name[128];
        double value;
    } cat_item_t;

    cat_item_t cat_arr[128];
    int        cat_count = 0;
    double     total_assets = 0;
    size_t     n = csilk_json_array_size(rows);

    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        const char*   cat_name = csilk_json_get_string(row, "category_name");
        const char*   cur = csilk_json_get_string(row, "currency");
        double        v = db_get_num(row, "value") * exchange_rate_get_to_cny(cur);
        total_assets += v;
        if (cat_name) {
            int found = -1;
            for (int k = 0; k < cat_count; k++) {
                if (strcmp(cat_arr[k].name, cat_name) == 0) {
                    found = k;
                    break;
                }
            }
            if (found >= 0) {
                cat_arr[found].value += v;
            } else if (cat_count < 128) {
                strncpy(cat_arr[cat_count].name, cat_name, sizeof(cat_arr[0].name) - 1);
                cat_arr[cat_count].name[sizeof(cat_arr[0].name) - 1] = '\0';
                cat_arr[cat_count].value = v;
                cat_count++;
            }
        }
    }
    csilk_json_free(rows);

    csilk_json_t* breakdown = csilk_json_array();
    for (int k = 0; k < cat_count; k++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "category", cat_arr[k].name);
        csilk_json_add_number(item, "value", cat_arr[k].value);
        csilk_json_array_append(breakdown, item);
    }

    double total_liabilities =
        portfolio_repo_get_total_liabilities(db_pool, cmd->user_id, cmd->ledger_id);
    double net_worth = total_assets - total_liabilities;

    csilk_json_t* recent_tx =
        portfolio_repo_get_recent_transactions(db_pool, cmd->user_id, cmd->ledger_id, 5);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "total_assets", mf_round_to_decimals(total_assets, 2));
    csilk_json_add_number(resp, "total_liabilities", mf_round_to_decimals(total_liabilities, 2));
    csilk_json_add_number(resp, "net_worth", mf_round_to_decimals(net_worth, 2));
    csilk_json_add_array(resp, "category_breakdown", breakdown);
    csilk_json_add_array(resp, "recent_transactions", recent_tx ? recent_tx : csilk_json_array());

    csilk_json_t* trend_rows = portfolio_repo_get_trend_30d(db_pool, cmd->user_id, cmd->ledger_id);
    if (trend_rows && csilk_json_array_size(trend_rows) > 0) {
        const char* trend_str = csilk_json_get_string(csilk_json_array_get(trend_rows, 0), "trend");
        csilk_json_t* trend_arr = (trend_str && trend_str[0]) ? csilk_json_parse(trend_str) : NULL;
        if (trend_arr) {
            csilk_json_add_array(resp, "trend", trend_arr);
        } else {
            csilk_json_add_array(resp, "trend", csilk_json_array());
        }
    } else {
        csilk_json_add_array(resp, "trend", csilk_json_array());
    }
    if (trend_rows) {
        csilk_json_free(trend_rows);
    }

    *out_resp = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}
