#include "infrastructure/repositories/portfolio_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mf_portfolio_repo_get_holdings(void* db_pool, int64_t user_id, mf_holding_item_t** out_items, size_t* out_count) {
    if (!db_pool || !out_items || !out_count) return -1;
    *out_items = NULL;
    *out_count = 0;

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    const char* hold_sql =
        "SELECT a.id AS asset_id, a.name, c.asset_type, a.currency, "
        "a.quantity, a.net_value, a.cost_basis "
        "FROM assets a JOIN categories c ON a.category_id = c.id "
        "WHERE a.user_id = ? AND c.asset_type IN ('stock','fund','bond','crypto') "
        "ORDER BY a.id ASC";

    csilk_json_t* hold_rows = csilk_db_query_param_json((csilk_db_pool_t*)db_pool, hold_sql, params);
    if (!hold_rows) return -1;

    size_t count = csilk_json_array_size(hold_rows);
    if (count == 0) {
        csilk_json_free(hold_rows);
        return 0;
    }

    mf_holding_item_t* items = (mf_holding_item_t*)calloc(count, sizeof(mf_holding_item_t));
    if (!items) {
        csilk_json_free(hold_rows);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        csilk_json_t* row = csilk_json_array_get(hold_rows, i);
        items[i].asset_id = (int64_t)db_get_num(row, "asset_id");
        const char* name = csilk_json_get_string(row, "name");
        if (name) snprintf(items[i].name, sizeof(items[i].name), "%s", name);
        const char* atype = csilk_json_get_string(row, "asset_type");
        if (atype) snprintf(items[i].asset_type, sizeof(items[i].asset_type), "%s", atype);
        const char* cur = csilk_json_get_string(row, "currency");
        items[i].currency = currency_from_str(cur ? cur : "CNY");

        quantity_from_double(db_get_num(row, "quantity"), 4, &items[i].quantity);
        price_from_double(db_get_num(row, "net_value"), 4, items[i].currency, &items[i].net_value);
        money_from_double(db_get_num(row, "cost_basis"), items[i].currency, &items[i].cost_basis);
    }

    csilk_json_free(hold_rows);
    *out_items = items;
    *out_count = count;
    return 0;
}

void mf_portfolio_repo_free_holdings(mf_holding_item_t* items, size_t count) {
    (void)count;
    if (items) free(items);
}

int mf_portfolio_repo_get_trade_events(void* db_pool, int64_t user_id, mf_portfolio_trade_event_t** out_events, size_t* out_count) {
    if (!db_pool || !out_events || !out_count) return -1;
    *out_events = NULL;
    *out_count = 0;

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    const char* tx_sql =
        "SELECT asset_id, transaction_type, quantity, amount, price_per_unit, fee, currency "
        "FROM transactions WHERE user_id = ? ORDER BY transaction_date ASC";

    csilk_json_t* tx_rows = csilk_db_query_param_json((csilk_db_pool_t*)db_pool, tx_sql, params);
    if (!tx_rows) return -1;

    size_t count = csilk_json_array_size(tx_rows);
    if (count == 0) {
        csilk_json_free(tx_rows);
        return 0;
    }

    mf_portfolio_trade_event_t* events = (mf_portfolio_trade_event_t*)calloc(count, sizeof(mf_portfolio_trade_event_t));
    if (!events) {
        csilk_json_free(tx_rows);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        csilk_json_t* row = csilk_json_array_get(tx_rows, i);
        events[i].asset_id = (int64_t)db_get_num(row, "asset_id");
        const char* type = csilk_json_get_string(row, "transaction_type");
        if (type) snprintf(events[i].type, sizeof(events[i].type), "%s", type);
        const char* cur_s = csilk_json_get_string(row, "currency");
        currency_t cur = currency_from_str(cur_s ? cur_s : "CNY");

        quantity_from_double(db_get_num(row, "quantity"), 4, &events[i].quantity);
        money_from_double(db_get_num(row, "amount"), cur, &events[i].amount);
        price_from_double(db_get_num(row, "price_per_unit"), 4, cur, &events[i].price);
        money_from_double(db_get_num(row, "fee"), cur, &events[i].fee);
    }

    csilk_json_free(tx_rows);
    *out_events = events;
    *out_count = count;
    return 0;
}

void mf_portfolio_repo_free_trade_events(mf_portfolio_trade_event_t* events, size_t count) {
    (void)count;
    if (events) free(events);
}

csilk_json_t* portfolio_repo_get_category_assets(void* pool, int64_t user_id) {
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT c.name as category_name, a.current_value as value, a.currency as currency "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type NOT IN ('loan','credit_card','other_liability')",
        params);
}

double portfolio_repo_get_total_liabilities(void* pool, int64_t user_id) {
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    csilk_json_t* liab_rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT COALESCE(SUM(a.current_value), 0) as total_liabilities "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability')",
        params);
    double total = 0.0;
    if (liab_rows) {
        if (csilk_json_array_size(liab_rows) > 0) {
            total = db_get_num(csilk_json_array_get(liab_rows, 0), "total_liabilities");
        }
        csilk_json_free(liab_rows);
    }
    return total;
}

csilk_json_t* portfolio_repo_get_recent_transactions(void* pool, int64_t user_id, int limit) {
    char uid_str[32], lim_str[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lim_str, sizeof(lim_str), "%d", limit > 0 ? limit : 5);
    const char* params[] = {uid_str, lim_str, NULL};

    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT t.id, t.transaction_date, t.transaction_type, t.amount, t.currency, "
        "a.name as asset_name, c.name as category_name "
        "FROM transactions t "
        "LEFT JOIN assets a ON t.asset_id=a.id "
        "LEFT JOIN categories c ON t.category_id=c.id "
        "WHERE t.user_id=? "
        "ORDER BY t.transaction_date DESC, t.id DESC LIMIT ?",
        params);
}

csilk_json_t* portfolio_repo_get_trend_30d(void* pool, int64_t user_id) {
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, uid_str, uid_str, uid_str, NULL};

    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT json_group_array(json_object("
        "'date', day, "
        "'total_assets', assets, "
        "'total_liabilities', liabilities, "
        "'net_worth', assets - liabilities"
        ")) as trend "
        "FROM ("
        "  WITH RECURSIVE days(d) AS ("
        "    SELECT date('now', '-29 days')"
        "    UNION ALL"
        "    SELECT date(d, '+1 day') FROM days WHERE d < date('now')"
        "  )"
        "  SELECT "
        "    days.d as day,"
        "    COALESCE((SELECT SUM(current_value) FROM assets a JOIN categories c ON a.category_id=c.id "
        "     WHERE a.user_id=? AND c.asset_type NOT IN ('loan','credit_card','other_liability') "
        "     AND date(a.created_at) <= days.d), 0) as assets,"
        "    COALESCE((SELECT SUM(current_value) FROM assets a JOIN categories c ON a.category_id=c.id "
        "     WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability') "
        "     AND date(a.created_at) <= days.d), 0) as liabilities "
        "  FROM days "
        "  ORDER BY days.d ASC"
        ")",
        params);
}

csilk_json_t* portfolio_repo_get_performance_transactions(void* pool, int64_t user_id) {
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    return csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT t.id, a.name as asset_name, t.transaction_type, t.direction, t.transaction_date, "
        "t.quantity, t.price_per_unit, t.amount, t.fee "
        "FROM transactions t JOIN assets a ON t.asset_id=a.id "
        "WHERE t.user_id=? "
        "ORDER BY t.transaction_date ASC",
        params);
}

int portfolio_repo_get_current_holdings_totals(void* pool, int64_t user_id,
                                              double* out_qty, double* out_cost, double* out_market) {
    if (!out_qty || !out_cost || !out_market) return -1;
    *out_qty = 0.0;
    *out_cost = 0.0;
    *out_market = 0.0;

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, NULL};

    csilk_json_t* pos_rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT COALESCE(SUM(quantity),0) as total_qty, "
        "COALESCE(SUM(cost_basis),0) as total_cost, "
        "COALESCE(SUM(current_value),0) as total_market "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('stock','fund','bond','crypto')",
        params);

    if (pos_rows && csilk_json_array_size(pos_rows) > 0) {
        const csilk_json_t* pr = csilk_json_array_get(pos_rows, 0);
        *out_market = db_get_num(pr, "total_market");
        *out_cost = db_get_num(pr, "total_cost");
        *out_qty = db_get_num(pr, "total_qty");
        csilk_json_free(pos_rows);
        return 0;
    }
    if (pos_rows) csilk_json_free(pos_rows);
    return -1;
}
