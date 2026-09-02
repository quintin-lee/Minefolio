#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "services/market/exchange_rate_service.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {

    int64_t asset_id;

    double cost_for_pnl;

    double qty;

    double realized;

} holding_pnl_t;

void
report_asset_trend(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    const char* period_str = csilk_get_query(c, "period");
    int         days = 30;
    if (period_str) {
        if (strcmp(period_str, "90d") == 0) {
            days = 90;
        } else if (strcmp(period_str, "365d") == 0) {
            days = 365;
        } else {
            days = atoi(period_str);
        }
    }
    if (days <= 0 || days > 365) {
        days = 30;
    }

    char days_str[32], uid_str[32];
    snprintf(days_str, sizeof(days_str), "%d", days - 1);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {days_str, uid_str, days_str, uid_str, days_str, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    result = csilk_db_query_param_json(
        pool,
        "SELECT json_group_array(d) as labels, "
        "json_group_array(total_assets) as assets, "
        "json_group_array(total_liabilities) as liabilities, "
        "json_group_array(total_assets - total_liabilities) as net_worth "
        "FROM ( "
        "WITH RECURSIVE pts(n) AS (SELECT 0 UNION ALL SELECT n+1 FROM pts WHERE n < 4) "
        "SELECT date('now','-'||(n*(?)/4)||' days') as d, "
        "(SELECT COALESCE(SUM(a.current_value),0) FROM assets a "
        "JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type NOT IN ('loan','credit_card','other_liability') "
        "AND a.updated_at < date('now','-'||(n*(?)/4)||' days','+1 day')) as total_assets, "
        "(SELECT COALESCE(SUM(a.current_value),0) FROM assets a "
        "JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability') "
        "AND a.updated_at < date('now','-'||(n*(?)/4)||' days','+1 day')) as total_liabilities "
        "FROM pts) ORDER BY d",
        params);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", period_str ? period_str : "30d");
    if (result && csilk_json_array_size(result) > 0) {
        csilk_json_t* row = csilk_json_array_get(result, 0);
        const char*   labels_str = csilk_json_get_string(row, "labels");
        const char*   nw_str = csilk_json_get_string(row, "net_worth");
        const char*   assets_str = csilk_json_get_string(row, "assets");
        const char*   liabs_str = csilk_json_get_string(row, "liabilities");
        csilk_json_t* labels = (labels_str && labels_str[0]) ? csilk_json_parse(labels_str) : NULL;
        csilk_json_t* nw = (nw_str && nw_str[0]) ? csilk_json_parse(nw_str) : NULL;
        csilk_json_t* assets = (assets_str && assets_str[0]) ? csilk_json_parse(assets_str) : NULL;
        csilk_json_t* liabs = (liabs_str && liabs_str[0]) ? csilk_json_parse(liabs_str) : NULL;
        csilk_json_add_array(resp, "labels", labels ? labels : csilk_json_array());
        csilk_json_add_array(resp, "net_worth", nw ? nw : csilk_json_array());
        csilk_json_add_array(resp, "assets", assets ? assets : csilk_json_array());
        csilk_json_add_array(resp, "liabilities", liabs ? liabs : csilk_json_array());
    } else {
        csilk_json_add_array(resp, "labels", csilk_json_array());
        csilk_json_add_array(resp, "net_worth", csilk_json_array());
        csilk_json_add_array(resp, "assets", csilk_json_array());
        csilk_json_add_array(resp, "liabilities", csilk_json_array());
    }
    if (result) {
        csilk_json_free(result);
    }
    respond_ok(c, resp);
}

void
report_asset_breakdown(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    csilk_json_t* assets = csilk_db_query_param_json(
        pool,
        "SELECT c.name as name, a.current_value as value, a.currency as currency "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type NOT IN ('loan','credit_card','other_liability')",
        params);
    csilk_json_t* liabs = csilk_db_query_param_json(
        pool,
        "SELECT c.name as name, a.current_value as value, a.currency as currency "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability')",
        params);

    typedef struct {
        char   name[128];
        double value;
    } cat_acc_t;

    cat_acc_t asset_cats[128];
    int       asset_cats_count = 0;
    double    total_assets = 0;

    if (assets) {
        size_t n = csilk_json_array_size(assets);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(assets, i);
            const char*   name = csilk_json_get_string(row, "name");
            const char*   cur = csilk_json_get_string(row, "currency");
            double        v = db_get_num(row, "value") * exchange_rate_get_to_cny(cur);
            total_assets += v;
            if (name) {
                int found = -1;
                for (int k = 0; k < asset_cats_count; k++) {
                    if (strcmp(asset_cats[k].name, name) == 0) {
                        found = k;
                        break;
                    }
                }
                if (found >= 0) {
                    asset_cats[found].value += v;
                } else if (asset_cats_count < 128) {
                    strncpy(
                        asset_cats[asset_cats_count].name, name, sizeof(asset_cats[0].name) - 1);
                    asset_cats[asset_cats_count].name[sizeof(asset_cats[0].name) - 1] = '\0';
                    asset_cats[asset_cats_count].value = v;
                    asset_cats_count++;
                }
            }
        }
        csilk_json_free(assets);
    }

    cat_acc_t liab_cats[128];
    int       liab_cats_count = 0;
    double    total_liabs = 0;

    if (liabs) {
        size_t n = csilk_json_array_size(liabs);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(liabs, i);
            const char*   name = csilk_json_get_string(row, "name");
            const char*   cur = csilk_json_get_string(row, "currency");
            double        v = db_get_num(row, "value") * exchange_rate_get_to_cny(cur);
            total_liabs += v;
            if (name) {
                int found = -1;
                for (int k = 0; k < liab_cats_count; k++) {
                    if (strcmp(liab_cats[k].name, name) == 0) {
                        found = k;
                        break;
                    }
                }
                if (found >= 0) {
                    liab_cats[found].value += v;
                } else if (liab_cats_count < 128) {
                    strncpy(liab_cats[liab_cats_count].name, name, sizeof(liab_cats[0].name) - 1);
                    liab_cats[liab_cats_count].name[sizeof(liab_cats[0].name) - 1] = '\0';
                    liab_cats[liab_cats_count].value = v;
                    liab_cats_count++;
                }
            }
        }
        csilk_json_free(liabs);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_t* asset_items = csilk_json_array();
    for (int i = 0; i < asset_cats_count; i++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", asset_cats[i].name);
        csilk_json_add_number(item, "value", asset_cats[i].value);
        csilk_json_add_number(
            item, "pct", total_assets > 0 ? (asset_cats[i].value / total_assets * 100) : 0);
        csilk_json_array_append(asset_items, item);
    }
    csilk_json_add_array(resp, "assets", asset_items);

    csilk_json_t* liab_items = csilk_json_array();
    for (int i = 0; i < liab_cats_count; i++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", liab_cats[i].name);
        csilk_json_add_number(item, "value", liab_cats[i].value);
        csilk_json_add_number(
            item, "pct", total_liabs > 0 ? (liab_cats[i].value / total_liabs * 100) : 0);
        csilk_json_array_append(liab_items, item);
    }
    csilk_json_add_array(resp, "liabilities", liab_items);
    csilk_json_add_number(resp, "total_assets", total_assets);
    csilk_json_add_number(resp, "total_liabilities", total_liabs);
    csilk_json_add_number(resp, "net_worth", total_assets - total_liabs);
    respond_ok(c, resp);
}

void
report_transaction_performance(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    csilk_json_t* result = csilk_db_query_param_json(
        pool,
        "SELECT t.id, a.name as asset_name, t.transaction_type, t.direction, t.transaction_date, "
        "t.quantity, t.price_per_unit, t.amount, t.fee "
        "FROM transactions t JOIN assets a ON t.asset_id=a.id "
        "WHERE t.user_id=? "
        "ORDER BY t.transaction_date ASC",
        params);
    if (!result) {
        respond_error(c, 500, "查询失败");
        return;
    }

    double        total_gain = 0, total_loss = 0;
    double        total_cost_basis = 0;   // for display (includes fee, matches DB cost_basis)
    double        total_cost_for_pnl = 0; // for PnL avg_cost (excludes fee in numerator)
    double        total_quantity = 0;
    double        total_realized_pnl = 0;
    int           total_trades = 0;
    csilk_json_t* trades = csilk_json_array();
    size_t        n = csilk_json_array_size(result);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(result, i);
        const char*   type = csilk_json_get_string(row, "transaction_type");
        double        amt = db_get_num(row, "amount");
        double        fee = db_get_num(row, "fee");
        const char*   dir = csilk_json_get_string(row, "direction");
        double        qty = db_get_num(row, "quantity");
        double        price = db_get_num(row, "price_per_unit");
        const char*   date_s = csilk_json_get_string(row, "transaction_date");
        // 本金流向（存入/取出/转入/转出）不计入盈亏
        int is_principal = (strcmp(type, "deposit") == 0 || strcmp(type, "withdrawal") == 0 ||
                            strcmp(type, "transfer_in") == 0 || strcmp(type, "transfer_out") == 0);
        if (!is_principal) {
            if (dir && strcmp(dir, "in") == 0) {
                total_gain += amt;
            } else {
                total_loss += amt;
            }
        }
        total_trades++;

        // 持仓盈亏上下文
        if (strcmp(type, "buy") == 0 && qty > 0) {
            total_cost_basis += amt + fee; // database cost_basis includes fee
            total_cost_for_pnl += amt;     // PnL avg_cost excludes fee
            total_quantity += qty;
        } else if (strcmp(type, "sell") == 0 && qty > 0) {
            // avg_cost for PnL: uses cost_for_pnl (excludes fee)
            double avg_cost = total_quantity > 0 ? total_cost_for_pnl / total_quantity : 0;
            total_realized_pnl += amt - qty * avg_cost;
            // Reduce display cost_basis proportionally on sell (includes fee portion)
            double cost_reduction =
                total_quantity > 0 ? (total_cost_basis / total_quantity) * qty : 0;
            total_cost_basis -= cost_reduction;
            total_quantity -= qty;
        } else if (strcmp(type, "income") == 0) {
            // 分红视为成本返还
            total_cost_for_pnl -= amt;
            total_realized_pnl += amt;
        }

        // avg_cost_at_trade: buy 为均价，sell 为售出均价
        double avg_cost = 0, realized = 0;
        if (strcmp(type, "buy") == 0) {
            avg_cost = qty > 0 ? (amt + fee) / qty : 0;
        } else if (strcmp(type, "sell") == 0) {
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

    // 当前持仓市值与成本
    csilk_json_t* pos_rows = csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(SUM(quantity),0) as total_qty, "
        "COALESCE(SUM(cost_basis),0) as total_cost, "
        "COALESCE(SUM(current_value),0) as total_market "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('stock','fund','bond','crypto')",
        params);
    double market_value = 0, cost_basis_remaining = 0;
    if (pos_rows && csilk_json_array_size(pos_rows) > 0) {
        const csilk_json_t* pr = csilk_json_array_get(pos_rows, 0);
        market_value = db_get_num(pr, "total_market");
        cost_basis_remaining = db_get_num(pr, "total_cost");
        csilk_json_free(pos_rows);
    }

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
    respond_ok(c, resp);
}

void
report_asset_summary(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT c.name as name, c.asset_type, SUM(a.current_value) as value "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? GROUP BY c.name, c.asset_type",
        params);
    double        current_assets = 0, current_liabs = 0;
    csilk_json_t* by_cat = csilk_json_array();
    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(rows, i);
            double        v = db_get_num(row, "value");
            const char*   atype = csilk_json_get_string(row, "asset_type");
            int is_liab = (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                           strcmp(atype, "other_liability") == 0);
            if (is_liab) {
                current_liabs += v;
            } else {
                current_assets += v;
            }
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_string(item, "name", csilk_json_get_string(row, "name"));
            csilk_json_add_number(item, "value", v);
            csilk_json_add_bool(item, "is_liability", is_liab);
            csilk_json_array_append(by_cat, item);
        }
        csilk_json_free(rows);
    }
    // 30-day change estimate from transactions
    csilk_json_t* change_result =
        csilk_db_query_param_json(pool,
                                  "SELECT COALESCE(SUM(CASE WHEN direction='in' THEN amount ELSE "
                                  "-amount END),0) as net_change FROM transactions "
                                  "WHERE user_id=? AND transaction_date >= date('now','-30 days')",
                                  params);
    double change_30d = 0;
    if (change_result && csilk_json_array_size(change_result) > 0) {
        change_30d = db_get_num(csilk_json_array_get(change_result, 0), "net_change");
    }
    if (change_result) {
        csilk_json_free(change_result);
    }
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "current_value", current_assets - current_liabs);
    csilk_json_add_number(resp, "total_assets", current_assets);
    csilk_json_add_number(resp, "total_liabilities", current_liabs);
    csilk_json_add_number(resp, "change_30d", change_30d);
    double nw = current_assets - current_liabs;
    csilk_json_add_number(resp, "change_30d_pct", nw != 0 ? (change_30d / nw * 100) : 0);
    csilk_json_add_array(resp, "by_category", by_cat);
    respond_ok(c, resp);
}

void
report_multi_currency_summary(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    const char* base_cur = csilk_get_query(c, "base_currency");
    if (!base_cur || !base_cur[0]) {
        base_cur = "CNY";
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rows = csilk_db_query_param_json(
        pool,
        "SELECT a.id, a.name, a.currency, a.net_value, a.quantity, a.current_value, "
        "CASE WHEN a.quantity > 0 AND a.net_value > 0 THEN a.quantity * a.net_value ELSE "
        "a.current_value END as val, "
        "c.asset_type "
        "FROM assets a "
        "JOIN categories c ON a.category_id = c.id "
        "WHERE a.user_id = ?",
        params);

    typedef struct {
        char   currency[16];
        double assets;
        double liabilities;
        int    count;
    } cur_bucket_t;

    cur_bucket_t buckets[32];
    int          bucket_count = 0;
    memset(buckets, 0, sizeof(buckets));

    double grand_total_assets = 0;
    double grand_total_liabs = 0;

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(rows, i);
            const char*   cur = csilk_json_get_string(r, "currency");
            if (!cur || !cur[0]) {
                cur = "CNY";
            }
            double      val = db_get_num(r, "val");
            const char* atype = csilk_json_get_string(r, "asset_type");
            int         is_liab =
                (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                           strcmp(atype, "other_liability") == 0));

            int b_idx = -1;
            for (int b = 0; b < bucket_count; b++) {
                if (strcasecmp(buckets[b].currency, cur) == 0) {
                    b_idx = b;
                    break;
                }
            }
            if (b_idx < 0 && bucket_count < 32) {
                b_idx = bucket_count++;
                strncpy(buckets[b_idx].currency, cur, sizeof(buckets[b_idx].currency) - 1);
            }

            if (b_idx >= 0) {
                buckets[b_idx].count++;
                if (is_liab) {
                    buckets[b_idx].liabilities += val;
                } else {
                    buckets[b_idx].assets += val;
                }
            }

            double converted_val = exchange_rate_convert(val, cur, base_cur);
            if (is_liab) {
                grand_total_liabs += converted_val;
            } else {
                grand_total_assets += converted_val;
            }
        }
        csilk_json_free(rows);
    }

    double grand_net_worth = grand_total_assets - grand_total_liabs;

    csilk_json_t* cur_arr = csilk_json_array();
    for (int b = 0; b < bucket_count; b++) {
        double b_net = buckets[b].assets - buckets[b].liabilities;
        double converted_net = exchange_rate_convert(b_net, buckets[b].currency, base_cur);
        double rate_to_base = exchange_rate_convert(1.0, buckets[b].currency, base_cur);

        csilk_json_t* b_obj = csilk_json_object();
        csilk_json_add_string(b_obj, "currency", buckets[b].currency);
        csilk_json_add_number(b_obj, "asset_count", buckets[b].count);
        csilk_json_add_number(b_obj, "original_assets", buckets[b].assets);
        csilk_json_add_number(b_obj, "original_liabilities", buckets[b].liabilities);
        csilk_json_add_number(b_obj, "original_net_worth", b_net);
        csilk_json_add_number(b_obj, "rate_to_base", rate_to_base);
        csilk_json_add_number(b_obj, "converted_net_worth", converted_net);
        double pct = (grand_net_worth > 0) ? (converted_net / grand_net_worth * 100.0) : 0.0;
        csilk_json_add_number(b_obj, "percentage", pct);
        csilk_json_array_append(cur_arr, b_obj);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "base_currency", base_cur);
    csilk_json_add_number(resp, "total_net_worth", grand_net_worth);
    csilk_json_add_number(resp, "total_assets", grand_total_assets);
    csilk_json_add_number(resp, "total_liabilities", grand_total_liabs);
    csilk_json_add_array(resp, "currencies", cur_arr);

    respond_ok(c, resp);
}

void
report_fx_pnl(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }

    const char* base_cur = csilk_get_query(c, "base_currency");
    if (!base_cur || !base_cur[0]) {
        base_cur = "CNY";
    }

    csilk_db_pool_t* pool = db_get_pool();
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT a.id, a.name, a.currency, a.quantity, a.cost_basis, a.net_value, a.current_value, "
        "       c.name as category_name, c.asset_type, "
        "       (SELECT rate FROM exchange_rate_history h WHERE h.target_currency = a.currency "
        "ORDER BY h.rate_date ASC LIMIT 1) as initial_rate "
        "FROM assets a "
        "JOIN categories c ON a.category_id = c.id "
        "WHERE a.user_id = ? AND a.currency IS NOT NULL AND a.currency != ? AND a.currency != ''",
        (const char*[]){uid_str, base_cur, NULL});

    csilk_json_t* items = csilk_json_array();
    double        total_foreign_cost_base = 0.0;
    double        total_foreign_market_base = 0.0;
    double        total_asset_pnl_base = 0.0;
    double        total_fx_pnl_base = 0.0;
    double        total_combined_pnl_base = 0.0;

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(rows, i);
            int64_t       aid = db_get_int(r, "id");
            const char*   aname = csilk_json_get_string(r, "name");
            const char*   cur = csilk_json_get_string(r, "currency");
            const char*   cname = csilk_json_get_string(r, "category_name");
            double        qty = db_get_num(r, "quantity");
            double        cost = db_get_num(r, "cost_basis");
            double        nv = db_get_num(r, "net_value");
            double        cv = db_get_num(r, "current_value");

            double current_val_orig = (cv > 0) ? cv : (qty > 0 && nv > 0 ? qty * nv : 0.0);
            if (current_val_orig == 0.0 && cost == 0.0) {
                continue;
            }

            double current_fx_rate = exchange_rate_convert(1.0, cur, base_cur);
            double cost_fx_rate = db_get_num(r, "initial_rate");
            if (cost_fx_rate <= 0.0) {
                cost_fx_rate = current_fx_rate * 0.98; /* Fallback baseline */
            }

            double orig_pnl = current_val_orig - cost;
            double asset_pnl_base = orig_pnl * current_fx_rate;
            double fx_pnl_base = (cost > 0) ? cost * (current_fx_rate - cost_fx_rate)
                                            : (current_val_orig * (current_fx_rate - cost_fx_rate));
            double combined_pnl_base = asset_pnl_base + fx_pnl_base;

            double cost_in_base = cost * cost_fx_rate;
            double val_in_base = current_val_orig * current_fx_rate;

            total_foreign_cost_base += cost_in_base;
            total_foreign_market_base += val_in_base;
            total_asset_pnl_base += asset_pnl_base;
            total_fx_pnl_base += fx_pnl_base;
            total_combined_pnl_base += combined_pnl_base;

            csilk_json_t* it = csilk_json_object();
            csilk_json_add_number(it, "asset_id", (double)aid);
            csilk_json_add_string(it, "asset_name", aname ? aname : "");
            csilk_json_add_string(it, "currency", cur ? cur : "");
            csilk_json_add_string(it, "category_name", cname ? cname : "");
            csilk_json_add_number(it, "cost_basis_orig", cost);
            csilk_json_add_number(it, "current_value_orig", current_val_orig);
            csilk_json_add_number(it, "current_fx_rate", current_fx_rate);
            csilk_json_add_number(it, "cost_fx_rate", cost_fx_rate);
            csilk_json_add_number(it, "asset_pnl_base", asset_pnl_base);
            csilk_json_add_number(it, "fx_pnl_base", fx_pnl_base);
            csilk_json_add_number(it, "combined_pnl_base", combined_pnl_base);
            double fx_pct = (cost_fx_rate > 0)
                                ? ((current_fx_rate - cost_fx_rate) / cost_fx_rate * 100.0)
                                : 0.0;
            csilk_json_add_number(it, "fx_return_rate", fx_pct);
            csilk_json_array_append(items, it);
        }
        csilk_json_free(rows);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "base_currency", base_cur);
    csilk_json_add_number(resp, "total_foreign_cost_base", total_foreign_cost_base);
    csilk_json_add_number(resp, "total_foreign_market_base", total_foreign_market_base);
    csilk_json_add_number(resp, "total_asset_pnl_base", total_asset_pnl_base);
    csilk_json_add_number(resp, "total_fx_pnl_base", total_fx_pnl_base);
    csilk_json_add_number(resp, "total_combined_pnl_base", total_combined_pnl_base);
    csilk_json_add_array(resp, "assets", items);

    respond_ok(c, resp);
}
