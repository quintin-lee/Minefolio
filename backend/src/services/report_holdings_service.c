#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
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

static int64_t
holding_find(holding_pnl_t* arr, size_t n, int64_t asset_id)

{

    for (size_t i = 0; i < n; i++) {

        if (arr[i].asset_id == asset_id) {

            return (int64_t)i;
        }
    }

    return -1;
}
void
summary_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params1[] = {uid_str, NULL};

    // Category breakdown (asset categories only; liabilities excluded from pie)
    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT c.name as category_name, SUM(a.current_value) as value "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type NOT IN ('loan','credit_card','other_liability') "
        "GROUP BY c.name ORDER BY value DESC",
        params1);
    if (!rows) {
        respond_error(c, 500, "查询失败");
        return;
    }
    double total_assets = 0, total_liabilities = 0;
    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        total_assets += db_get_num(row, "value");
    }
    csilk_json_t* breakdown = csilk_json_array();
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        double        v = db_get_num(row, "value");
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "category_name", csilk_json_get_string(row, "category_name"));
        csilk_json_add_number(item, "value", v);
        csilk_json_add_number(item, "pct", total_assets > 0 ? (v / total_assets * 100) : 0);
        csilk_json_array_append(breakdown, item);
    }
    csilk_json_free(rows);

    // Total liabilities for net worth
    csilk_json_t* liab_rows = csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(SUM(a.current_value),0) as total "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability')",
        params1);
    if (liab_rows && csilk_json_array_size(liab_rows) > 0) {
        total_liabilities = db_get_num(csilk_json_array_get(liab_rows, 0), "total");
        csilk_json_free(liab_rows);
    }

    // 30-day net worth trend (daily snapshots, asset counted from its updated_at)
    const char*   params2[] = {uid_str, uid_str, NULL};
    csilk_json_t* trend_rows = csilk_db_query_param_json(
        pool,
        "SELECT json_group_array(json_object('date', d, 'net_worth', nw)) as trend FROM ("
        "WITH RECURSIVE dates(i) AS (SELECT 0 UNION ALL SELECT i+1 FROM dates WHERE i < 29) "
        "SELECT date('now','-'||(29-i)||' days') as d, "
        "(SELECT COALESCE(SUM(a.current_value),0) FROM assets a JOIN categories c ON "
        "a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type NOT IN ('loan','credit_card','other_liability') "
        "AND a.updated_at < date('now','-'||(29-i)||' days','+1 day')) - "
        "(SELECT COALESCE(SUM(a.current_value),0) FROM assets a JOIN categories c ON "
        "a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability') "
        "AND a.updated_at < date('now','-'||(29-i)||' days','+1 day')) as nw "
        "FROM dates)",
        params2);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "total_assets", total_assets);
    csilk_json_add_number(resp, "total_liabilities", total_liabilities);
    csilk_json_add_number(resp, "net_worth", total_assets - total_liabilities);
    csilk_json_add_array(resp, "breakdown", breakdown);
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
    respond_ok(c, resp);
}

void
report_holdings(csilk_ctx_t* c)
{
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    /* 持仓行：投资类资产（不论 quantity 是否为 0）。
       市值 = net_value × quantity（不依赖 current_value 列，避免直接建仓/联动时漂移） */
    const char* hold_sql =
        "SELECT a.id AS asset_id, a.name, c.asset_type, a.currency, "
        "a.quantity, a.net_value, a.cost_basis "
        "FROM assets a JOIN categories c ON a.category_id = c.id "
        "WHERE a.user_id = ? AND c.asset_type IN ('stock','fund','bond','crypto') "
        "ORDER BY a.id ASC";
    csilk_json_t* hold_rows = csilk_db_query_param_json(pool, hold_sql, params);
    if (!hold_rows) {
        respond_error(c, 500, "查询失败");
        return;
    }

    size_t         hn = csilk_json_array_size(hold_rows);
    holding_pnl_t* accs = NULL;
    if (hn > 0) {
        accs = (holding_pnl_t*)calloc(hn, sizeof(holding_pnl_t));
        if (!accs) {
            respond_error(c, 500, "内存不足");
            csilk_json_free(hold_rows);
            return;
        }
        for (size_t i = 0; i < hn; i++) {
            csilk_json_t* row = csilk_json_array_get(hold_rows, i);
            accs[i].asset_id = (int64_t)db_get_num(row, "asset_id");
        }
    }

    /* 用户全部交易，按日期升序（全局序保持各资产内时序，与 performance 一致） */
    const char*   tx_sql = "SELECT asset_id, transaction_type, quantity, amount "
                           "FROM transactions WHERE user_id = ? ORDER BY transaction_date ASC";
    csilk_json_t* tx_rows = csilk_db_query_param_json(pool, tx_sql, params);
    if (tx_rows) {
        size_t tn = csilk_json_array_size(tx_rows);
        for (size_t i = 0; i < tn; i++) {
            csilk_json_t* t = csilk_json_array_get(tx_rows, i);
            const char*   type = csilk_json_get_string(t, "transaction_type");
            double        amt = db_get_num(t, "amount");
            double        qty = db_get_num(t, "quantity");
            int64_t       aid = (int64_t)db_get_num(t, "asset_id");
            if (!type) {
                continue;
            }
            if (strcmp(type, "buy") != 0 && strcmp(type, "sell") != 0 &&
                strcmp(type, "income") != 0) {
                continue; /* fee 等行跳过，与 performance 一致 */
            }
            int64_t idx = holding_find(accs, hn, aid);
            if (idx < 0) {
                continue;                      /* 非投资类资产的交易，不计入持仓报表 */
            }
            if (strcmp(type, "buy") == 0) {
                accs[idx].cost_for_pnl += amt; /* 不含 fee，与 performance 口径一致 */
                accs[idx].qty += qty;
            } else if (strcmp(type, "sell") == 0) {
                double avg_cost = accs[idx].qty > 0 ? accs[idx].cost_for_pnl / accs[idx].qty : 0.0;
                accs[idx].realized += amt - qty * avg_cost;
                accs[idx].qty -= qty;
            } else { /* income */
                accs[idx].cost_for_pnl -= amt;
                accs[idx].realized += amt;
            }
        }
        csilk_json_free(tx_rows);
    }

    /* 组装响应 */
    csilk_json_t* holdings = csilk_json_array();
    double        total_market = 0.0, total_cost = 0.0, total_floating = 0.0, total_realized = 0.0;

    for (size_t i = 0; i < hn; i++) {
        csilk_json_t* row = csilk_json_array_get(hold_rows, i);
        double        quantity = db_get_num(row, "quantity");
        double        net_value = db_get_num(row, "net_value");
        double        cost_basis = db_get_num(row, "cost_basis");
        /* 浮动盈亏 = (当前净值 − 持仓成本净值) × 数量 = net_value*quantity − cost_basis
           不依赖 current_value 列（该列可能因直接建仓/余额联动而漂移） */
        double market = net_value * quantity;
        double floating = market - cost_basis;
        double pct = (cost_basis == 0.0) ? 0.0 : (floating / cost_basis) * 100.0;

        total_market += market;
        total_cost += cost_basis;
        total_floating += floating;
        total_realized += accs[i].realized;

        csilk_json_t* h = csilk_json_object();
        csilk_json_add_number(h, "asset_id", db_get_num(row, "asset_id"));
        csilk_json_add_string(h, "name", csilk_json_get_string(row, "name"));
        csilk_json_add_string(h, "asset_type", csilk_json_get_string(row, "asset_type"));
        csilk_json_add_string(h, "currency", csilk_json_get_string(row, "currency"));
        csilk_json_add_number(h, "quantity", quantity);
        csilk_json_add_number(h, "net_value", net_value);
        csilk_json_add_number(h, "cost_basis", cost_basis);
        csilk_json_add_number(h, "current_value", market);
        csilk_json_add_number(h, "floating_pnl", floating);
        csilk_json_add_number(h, "floating_pct", pct);
        csilk_json_add_number(h, "realized_pnl", accs[i].realized);
        csilk_json_array_append(holdings, h);
    }
    csilk_json_free(hold_rows);
    free(accs);

    double        sum_pct = (total_cost == 0.0) ? 0.0 : (total_floating / total_cost) * 100.0;
    csilk_json_t* summary = csilk_json_object();
    csilk_json_add_number(summary, "total_market_value", total_market);
    csilk_json_add_number(summary, "total_cost_basis", total_cost);
    csilk_json_add_number(summary, "total_floating_pnl", total_floating);
    csilk_json_add_number(summary, "total_realized_pnl", total_realized);
    csilk_json_add_number(summary, "floating_pct", sum_pct);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_object(resp, "summary", summary);
    csilk_json_add_array(resp, "holdings", holdings);
    respond_ok(c, resp);
}
