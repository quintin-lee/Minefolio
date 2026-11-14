/**
 * @file report_asset_position.c
 * @brief 外币资产损益归因与持仓总览报表计算服务
 */

#include "services/report_asset_service.h"
#include "application/report/commands.h"
#include "common/db.h"
#include "services/market/exchange_rate_service.h"
#include "domain/portfolio/fx_table.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include "core/financial/percentage.h"
#include "core/financial/money.h"
#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "csilk/drivers/db.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void
report_fx_pnl(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    const char* base_cur = (q->base_currency && q->base_currency[0]) ? q->base_currency : "CNY";

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);

    const char* sql =
        db_is_postgres()
            ? "SELECT a.id, a.name, a.currency, a.quantity, a.cost_basis, a.net_value, "
              "a.current_value, "
              "       c.name as category_name, c.asset_type, "
              "       COALESCE("
              "           (SELECT rate FROM exchange_rate_history h WHERE h.target_currency = "
              "a.currency "
              "            AND h.rate_date <= COALESCE((SELECT to_char(MIN(t.transaction_date), "
              "'YYYY-MM-DD') FROM "
              "transactions t WHERE t.asset_id = a.id), to_char(a.created_at, 'YYYY-MM-DD')) "
              "            ORDER BY h.rate_date DESC LIMIT 1), "
              "           (SELECT rate FROM exchange_rate_history h WHERE h.target_currency = "
              "a.currency "
              "            ORDER BY h.rate_date ASC LIMIT 1) "
              "       ) as initial_rate "
              "FROM assets a "
              "JOIN categories c ON a.category_id = c.id "
              "WHERE a.user_id = ? AND a.ledger_id = ? AND a.currency IS NOT NULL AND a.currency "
              "!= ? AND a.currency "
              "!= ''"
            : "SELECT a.id, a.name, a.currency, a.quantity, a.cost_basis, a.net_value, "
              "a.current_value, "
              "       c.name as category_name, c.asset_type, "
              "       COALESCE("
              "           (SELECT rate FROM exchange_rate_history h WHERE h.target_currency = "
              "a.currency "
              "            AND h.rate_date <= COALESCE((SELECT date(MIN(t.transaction_date)) FROM "
              "transactions t WHERE t.asset_id = a.id), date(a.created_at)) "
              "            ORDER BY h.rate_date DESC LIMIT 1), "
              "           (SELECT rate FROM exchange_rate_history h WHERE h.target_currency = "
              "a.currency "
              "            ORDER BY h.rate_date ASC LIMIT 1) "
              "       ) as initial_rate "
              "FROM assets a "
              "JOIN categories c ON a.category_id = c.id "
              "WHERE a.user_id = ? AND a.ledger_id = ? AND a.currency IS NOT NULL AND a.currency "
              "!= ? AND a.currency "
              "!= ''";

    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool, sql, (const char*[]){uid_str, lid_str, base_cur, NULL});

    csilk_json_t* items = csilk_json_array();
    currency_t    base_currency = currency_from_str(base_cur);

    money_t total_foreign_cost_base = money_zero(base_currency);
    money_t total_foreign_market_base = money_zero(base_currency);
    money_t total_asset_pnl_base = money_zero(base_currency);
    money_t total_fx_pnl_base = money_zero(base_currency);
    money_t total_combined_pnl_base = money_zero(base_currency);

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(rows, i);
            int64_t       aid = db_get_int(r, "id");
            const char*   aname = csilk_json_get_string(r, "name");
            const char*   cur = csilk_json_get_string(r, "currency");
            const char*   cname = csilk_json_get_string(r, "category_name");
            currency_t    asset_cur = currency_from_str(cur);

            quantity_t q_qty = db_get_quantity(r, "quantity");
            money_t    cost = db_get_money(r, "cost_basis", asset_cur);
            price_t    nv = db_get_price(r, "net_value", asset_cur);
            money_t    cv = db_get_money(r, "current_value", asset_cur);

            money_t current_val_orig;
            if (!money_is_zero(cv)) {
                current_val_orig = cv;
            } else if (!quantity_is_zero(q_qty) && !decimal_is_zero(nv.unit_price)) {
                price_times_quantity(nv, q_qty, &current_val_orig);
            } else {
                current_val_orig = money_zero(asset_cur);
            }

            if (money_is_zero(current_val_orig) && money_is_zero(cost)) {
                continue;
            }

            rate_t    curr_fx_r = exchange_rate_get_rate(asset_cur, base_currency);
            rate_t    cost_fx_r;
            decimal_t init_rate_d = db_get_decimal(r, "initial_rate");
            if (!decimal_is_zero(init_rate_d)) {
                cost_fx_r = rate_from_decimal(init_rate_d, asset_cur, base_currency);
            } else {
                cost_fx_r = curr_fx_r;
            }

            money_t orig_pnl;
            money_sub(current_val_orig, cost, &orig_pnl);
            money_t asset_pnl_base;
            rate_convert_money(orig_pnl, curr_fx_r, &asset_pnl_base);

            rate_t r_diff;
            r_diff.from_currency = asset_cur;
            r_diff.to_currency = base_currency;
            decimal_sub(curr_fx_r.factor, cost_fx_r.factor, &r_diff.factor);

            money_t fx_pnl_base;
            if (!money_is_zero(cost)) {
                rate_convert_money(cost, r_diff, &fx_pnl_base);
            } else {
                rate_convert_money(current_val_orig, r_diff, &fx_pnl_base);
            }

            money_t combined_pnl_base;
            money_add(asset_pnl_base, fx_pnl_base, &combined_pnl_base);

            money_t cost_in_base, val_in_base;
            rate_convert_money(cost, cost_fx_r, &cost_in_base);
            rate_convert_money(current_val_orig, curr_fx_r, &val_in_base);

            money_add(total_foreign_cost_base, cost_in_base, &total_foreign_cost_base);
            money_add(total_foreign_market_base, val_in_base, &total_foreign_market_base);
            money_add(total_asset_pnl_base, asset_pnl_base, &total_asset_pnl_base);
            money_add(total_fx_pnl_base, fx_pnl_base, &total_fx_pnl_base);
            money_add(total_combined_pnl_base, combined_pnl_base, &total_combined_pnl_base);

            csilk_json_t* it = csilk_json_object();
            csilk_json_add_number(it, "asset_id", (double)aid);
            csilk_json_add_string(it, "asset_name", aname ? aname : "");
            csilk_json_add_string(it, "currency", cur ? cur : "");
            csilk_json_add_string(it, "category_name", cname ? cname : "");
            csilk_json_add_number(it, "cost_basis_orig", money_to_double(cost));
            csilk_json_add_number(it, "current_value_orig", money_to_double(current_val_orig));
            csilk_json_add_number(it, "current_fx_rate", rate_to_double(curr_fx_r));
            csilk_json_add_number(it, "cost_fx_rate", rate_to_double(cost_fx_r));
            csilk_json_add_number(it, "asset_pnl_base", money_to_double(asset_pnl_base));
            csilk_json_add_number(it, "fx_pnl_base", money_to_double(fx_pnl_base));
            csilk_json_add_number(it, "combined_pnl_base", money_to_double(combined_pnl_base));

            percentage_t fx_pct;
            if (!decimal_is_zero(cost_fx_r.factor)) {
                decimal_t diff_pct, hundred;
                hundred = decimal_from_int(100);
                decimal_div(r_diff.factor, cost_fx_r.factor, 4, ROUND_HALF_UP, &diff_pct);
                decimal_mul(diff_pct, hundred, &diff_pct);
                fx_pct = percentage_from_decimal(diff_pct);
            } else {
                fx_pct = percentage_zero();
            }
            csilk_json_add_number(it, "fx_return_rate", percentage_to_double(fx_pct));
            csilk_json_array_append(items, it);
        }
        csilk_json_free(rows);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "base_currency", base_cur);
    csilk_json_add_number(
        resp, "total_foreign_cost_base", money_to_double(total_foreign_cost_base));
    csilk_json_add_number(
        resp, "total_foreign_market_base", money_to_double(total_foreign_market_base));
    csilk_json_add_number(resp, "total_asset_pnl_base", money_to_double(total_asset_pnl_base));
    csilk_json_add_number(resp, "total_fx_pnl_base", money_to_double(total_fx_pnl_base));
    csilk_json_add_number(
        resp, "total_combined_pnl_base", money_to_double(total_combined_pnl_base));
    csilk_json_add_array(resp, "assets", items);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}

void
report_position_overview(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);
    const char* params[] = {uid_str, lid_str, NULL};

    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT a.id, a.name, a.current_value, a.currency, a.quantity, a.cost_basis, "
        "a.net_value, c.name as category_name, c.asset_type "
        "FROM assets a "
        "JOIN categories c ON a.category_id = c.id "
        "WHERE a.user_id = ? AND a.ledger_id = ?",
        params);

    double  total_assets = 0.0, total_liabs = 0.0;
    double  total_cost = 0.0;
    double  max_weight = 0.0;
    int64_t max_asset_id = 0;
    char    max_asset_name[128] = "";
    double  hhi = 0.0;
    int     asset_cnt = 0;

    typedef struct {
        char   name[128];
        double value;
    } cat_agg_t;

    cat_agg_t cats[128];
    int       cat_cnt = 0;

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* r = csilk_json_array_get(rows, i);
            const char*         cur = csilk_json_get_string(r, "currency");
            double              cv =
                db_get_num(r, "current_value") * exchange_rate_get_to_cny(cur ? cur : "CNY");
            const char* atype = csilk_json_get_string(r, "asset_type");
            int         is_liab =
                (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                           strcmp(atype, "other_liability") == 0));

            if (is_liab) {
                total_liabs += cv;
            } else {
                total_assets += cv;
                asset_cnt++;
                total_cost +=
                    db_get_num(r, "cost_basis") * exchange_rate_get_to_cny(cur ? cur : "CNY");

                double w = (total_assets > 0.0 && cv > 0.0) ? cv / total_assets : 0.0;
                hhi += w * w;
                if (w > max_weight) {
                    max_weight = w;
                    max_asset_id = db_get_int(r, "id");
                    const char* nm = csilk_json_get_string(r, "name");
                    snprintf(max_asset_name, sizeof(max_asset_name), "%s", nm ? nm : "");
                }

                const char* cat_name = csilk_json_get_string(r, "category_name");
                int         found = -1;
                for (int k = 0; k < cat_cnt; k++) {
                    if (strcmp(cats[k].name, cat_name ? cat_name : "") == 0) {
                        found = k;
                        break;
                    }
                }
                if (found >= 0) {
                    cats[found].value += cv;
                } else if (cat_cnt < 128) {
                    cats[cat_cnt].value = cv;
                    snprintf(cats[cat_cnt].name,
                             sizeof(cats[cat_cnt].name),
                             "%s",
                             cat_name ? cat_name : "");
                    cat_cnt++;
                }
            }
        }
        csilk_json_free(rows);
    }

    double net_worth = total_assets - total_liabs;
    double total_pnl = total_assets - total_cost;
    double total_return_pct = total_cost > 0.0 ? (total_pnl / total_cost * 100.0) : 0.0;

    char today_str[16];
    {
        time_t    now_t = time(NULL);
        struct tm tm_now;
        localtime_r(&now_t, &tm_now);
        strftime(today_str, sizeof(today_str), "%Y-%m-%d", &tm_now);
    }
    double daily_pnl = 0.0;
    {
        const char* today_sql =
            db_is_postgres() ? "SELECT c.asset_type, COALESCE(SUM(abl.delta), 0.0) as total_delta "
                               "FROM asset_balance_logs abl "
                               "JOIN assets a ON abl.asset_id = a.id "
                               "JOIN categories c ON a.category_id = c.id "
                               "WHERE abl.user_id = ? AND a.ledger_id = ? "
                               "AND abl.created_at::date = ?::date GROUP BY c.asset_type"
                             : "SELECT c.asset_type, COALESCE(SUM(abl.delta), 0.0) as total_delta "
                               "FROM asset_balance_logs abl "
                               "JOIN assets a ON abl.asset_id = a.id "
                               "JOIN categories c ON a.category_id = c.id "
                               "WHERE abl.user_id = ? AND a.ledger_id = ? "
                               "AND date(abl.created_at) = ? GROUP BY c.asset_type";
        csilk_json_t* dlog = csilk_db_query_param_json(
            (csilk_db_pool_t*)pool, today_sql, (const char*[]){uid_str, lid_str, today_str, NULL});
        if (dlog) {
            size_t dn = csilk_json_array_size(dlog);
            for (size_t i = 0; i < dn; i++) {
                const csilk_json_t* r = csilk_json_array_get(dlog, i);
                double              delta = db_get_num(r, "total_delta");
                const char*         atype = csilk_json_get_string(r, "asset_type");
                int                 is_liab =
                    (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                               strcmp(atype, "other_liability") == 0));
                daily_pnl += is_liab ? -delta : delta;
            }
            csilk_json_free(dlog);
        }
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "total_assets", total_assets);
    csilk_json_add_number(resp, "total_liabilities", total_liabs);
    csilk_json_add_number(resp, "net_worth", net_worth);
    csilk_json_add_number(resp, "total_cost", total_cost);
    csilk_json_add_number(resp, "total_pnl", total_pnl);
    csilk_json_add_number(resp, "total_return_pct", total_return_pct);
    csilk_json_add_number(resp, "daily_pnl", daily_pnl);
    csilk_json_add_number(resp, "hhi", hhi);
    csilk_json_add_number(resp, "max_holding_weight", max_weight);
    csilk_json_add_number(resp, "max_holding_asset_id", (double)max_asset_id);
    csilk_json_add_string(resp, "max_holding_name", max_asset_name);
    csilk_json_add_number(resp, "asset_count", (double)asset_cnt);

    csilk_json_t* cat_arr = csilk_json_array();
    for (int k = 0; k < cat_cnt; k++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", cats[k].name);
        csilk_json_add_number(item, "value", cats[k].value);
        csilk_json_add_number(
            item, "pct", total_assets > 0.0 ? (cats[k].value / total_assets * 100.0) : 0.0);
        csilk_json_array_append(cat_arr, item);
    }
    csilk_json_add_array(resp, "by_category", cat_arr);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}
