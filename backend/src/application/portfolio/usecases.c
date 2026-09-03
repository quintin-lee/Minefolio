#include "application/portfolio/usecases.h"
#include "domain/portfolio/repository.h"
#include "domain/portfolio/rules.h"
#include "infrastructure/repositories/portfolio_repo_impl.h"
#include "services/market/exchange_rate_service.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int portfolio_usecase_get_holdings(void* db_pool, const query_portfolio_cmd_t* cmd,
                                  csilk_json_t** out_resp, portfolio_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!cmd || !db_pool || !out_resp) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    mf_holding_item_t* items = NULL;
    size_t item_count = 0;
    if (mf_portfolio_repo_get_holdings(db_pool, cmd->user_id, &items, &item_count) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询持仓标的失败");
        return -1;
    }

    mf_portfolio_trade_event_t* events = NULL;
    size_t event_count = 0;
    if (mf_portfolio_repo_get_trade_events(db_pool, cmd->user_id, &events, &event_count) != 0) {
        mf_portfolio_repo_free_holdings(items, item_count);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询交易事实流水失败");
        return -1;
    }

    /* 纯领域规则：重放交易计算持仓盈亏 */
    mf_portfolio_rule_apply_trade_events(items, item_count, events, event_count);

    /* 纯领域规则：聚合组合整体汇总 */
    currency_t base_cur = currency_from_str(cmd->base_currency ? cmd->base_currency : "CNY");
    mf_portfolio_summary_t summary = {0};
    mf_portfolio_rule_aggregate_summary(items, item_count, base_cur, &summary);

    /* 组装响应 DTO */
    csilk_json_t* holdings_arr = csilk_json_array();
    for (size_t i = 0; i < item_count; i++) {
        const mf_holding_item_t* it = &items[i];
        csilk_json_t* h = csilk_json_object();
        csilk_json_add_number(h, "asset_id", (double)it->asset_id);
        csilk_json_add_string(h, "name", it->name);
        csilk_json_add_string(h, "asset_type", it->asset_type);
        csilk_json_add_string(h, "currency", currency_code(&it->currency));
        csilk_json_add_number(h, "quantity", quantity_to_double(it->quantity));
        csilk_json_add_number(h, "net_value", price_to_double(it->net_value));
        csilk_json_add_number(h, "cost_basis", money_to_double(it->cost_basis));
        csilk_json_add_number(h, "current_value", money_to_double(it->market_value));
        csilk_json_add_number(h, "floating_pnl", money_to_double(it->floating_pnl));
        csilk_json_add_number(h, "floating_pct", it->floating_pct);
        csilk_json_add_number(h, "realized_pnl", money_to_double(it->realized_pnl));
        csilk_json_array_append(holdings_arr, h);
    }

    csilk_json_t* summary_obj = csilk_json_object();
    csilk_json_add_number(summary_obj, "total_market_value", money_to_double(summary.total_market_value));
    csilk_json_add_number(summary_obj, "total_cost_basis", money_to_double(summary.total_cost_basis));
    csilk_json_add_number(summary_obj, "total_floating_pnl", money_to_double(summary.total_floating_pnl));
    csilk_json_add_number(summary_obj, "total_realized_pnl", money_to_double(summary.total_realized_pnl));
    csilk_json_add_number(summary_obj, "floating_pct", summary.floating_pct);

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

int portfolio_usecase_get_performance(void* db_pool, const query_portfolio_cmd_t* cmd,
                                     csilk_json_t** out_resp, portfolio_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!cmd || !db_pool || !out_resp) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    csilk_json_t* result = portfolio_repo_get_performance_transactions(db_pool, cmd->user_id);
    if (!result) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询失败");
        return -1;
    }

    double total_gain = 0, total_loss = 0;
    double total_cost_basis = 0;
    double total_cost_for_pnl = 0;
    double total_quantity = 0;
    double total_realized_pnl = 0;
    int    total_trades = 0;
    csilk_json_t* trades = csilk_json_array();
    size_t n = csilk_json_array_size(result);

    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(result, i);
        const char* type = csilk_json_get_string(row, "transaction_type");
        double amt = db_get_num(row, "amount");
        double fee = db_get_num(row, "fee");
        const char* dir = csilk_json_get_string(row, "direction");
        double qty = db_get_num(row, "quantity");
        double price = db_get_num(row, "price_per_unit");
        const char* date_s = csilk_json_get_string(row, "transaction_date");

        int is_principal = (type && (strcmp(type, "deposit") == 0 || strcmp(type, "withdrawal") == 0 ||
                                     strcmp(type, "transfer_in") == 0 || strcmp(type, "transfer_out") == 0));
        if (!is_principal) {
            if (dir && strcmp(dir, "in") == 0) {
                total_gain += amt;
            } else {
                total_loss += amt;
            }
        }
        total_trades++;

        if (type && strcmp(type, "buy") == 0 && qty > 0) {
            total_cost_basis += amt + fee;
            total_cost_for_pnl += amt;
            total_quantity += qty;
        } else if (type && strcmp(type, "sell") == 0 && qty > 0) {
            double avg_cost = total_quantity > 0 ? total_cost_for_pnl / total_quantity : 0;
            total_realized_pnl += amt - qty * avg_cost;
            double cost_reduction = total_quantity > 0 ? (total_cost_basis / total_quantity) * qty : 0;
            total_cost_basis -= cost_reduction;
            total_quantity -= qty;
        } else if (type && strcmp(type, "income") == 0) {
            total_cost_for_pnl -= amt;
            total_realized_pnl += amt;
        }

        double avg_cost = 0, realized = 0;
        if (type && strcmp(type, "buy") == 0) {
            avg_cost = qty > 0 ? (amt + fee) / qty : 0;
        } else if (type && strcmp(type, "sell") == 0) {
            avg_cost = qty > 0 ? price : 0;
            realized = amt - qty * price - fee;
        }

        csilk_json_t* trade = csilk_json_object();
        csilk_json_add_number(trade, "id", db_get_num(row, "id"));
        csilk_json_add_string(trade, "asset_name", csilk_json_get_string(row, "asset_name"));
        csilk_json_add_string(trade, "type", type ? type : "");
        csilk_json_add_string(trade, "date", date_s ? date_s : "");
        csilk_json_add_number(trade, "quantity", qty);
        csilk_json_add_number(trade, "price", price);
        csilk_json_add_number(trade, "amount", amt);
        csilk_json_add_number(trade, "avg_cost_at_trade", avg_cost);
        csilk_json_add_number(trade, "realized", realized);
        csilk_json_add_number(trade, "fee", fee);
        csilk_json_array_append(trades, trade);
    }
    csilk_json_free(result);

    double market_value = 0, cost_basis_remaining = 0, dummy_qty = 0;
    portfolio_repo_get_current_holdings_totals(db_pool, cmd->user_id, &dummy_qty, &cost_basis_remaining, &market_value);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "total_trades", total_trades);
    csilk_json_add_number(resp, "total_gain", total_gain);
    csilk_json_add_number(resp, "total_loss", total_loss);
    csilk_json_add_number(resp, "net_gain", total_gain - total_loss);
    csilk_json_add_number(resp, "total_cost_basis_remaining", cost_basis_remaining);
    csilk_json_add_number(resp, "total_market_value", market_value);
    csilk_json_add_number(resp, "floating_pnl", market_value - cost_basis_remaining);
    csilk_json_add_number(resp, "realized_pnl", total_realized_pnl);
    csilk_json_add_array(resp, "trades", trades);

    *out_resp = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int portfolio_usecase_get_dashboard_summary(void* db_pool, const query_portfolio_cmd_t* cmd,
                                           csilk_json_t** out_resp, portfolio_usecase_result_t* out_res) {
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!cmd || !db_pool || !out_resp) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    csilk_json_t* rows = portfolio_repo_get_category_assets(db_pool, cmd->user_id);
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
        const char* cat_name = csilk_json_get_string(row, "category_name");
        const char* cur = csilk_json_get_string(row, "currency");
        double v = db_get_num(row, "value") * exchange_rate_get_to_cny(cur);
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

    double total_liabilities = portfolio_repo_get_total_liabilities(db_pool, cmd->user_id);
    double net_worth = total_assets - total_liabilities;

    csilk_json_t* recent_tx = portfolio_repo_get_recent_transactions(db_pool, cmd->user_id, 5);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "total_assets", total_assets);
    csilk_json_add_number(resp, "total_liabilities", total_liabilities);
    csilk_json_add_number(resp, "net_worth", net_worth);
    csilk_json_add_array(resp, "category_breakdown", breakdown);
    csilk_json_add_array(resp, "recent_transactions", recent_tx ? recent_tx : csilk_json_array());

    csilk_json_t* trend_rows = portfolio_repo_get_trend_30d(db_pool, cmd->user_id);
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
