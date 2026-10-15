#include "infrastructure/repositories/portfolio_repo_impl.h"
#include "common/db.h"
#include "services/market/exchange_rate_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int
mf_portfolio_repo_get_holdings(void*               db_pool,
                               int64_t             user_id,
                               int64_t             ledger_id,
                               mf_holding_item_t** out_items,
                               size_t*             out_count)
{
    if (!db_pool || !out_items || !out_count) {
        return -1;
    }
    *out_items = NULL;
    *out_count = 0;

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* params[4];
    int         pidx = 0;
    params[pidx++] = uid_str;

    char hold_sql[512];
    if (ledger_id > 0) {
        snprintf(hold_sql,
                 sizeof(hold_sql),
                 "SELECT a.id AS asset_id, a.name, c.asset_type, a.currency, "
                 "a.quantity, a.net_value, a.cost_basis "
                 "FROM assets a JOIN categories c ON a.category_id = c.id "
                 "WHERE a.user_id = ? AND a.ledger_id = ? AND c.asset_type IN "
                 "('stock','fund','bond','crypto') "
                 "ORDER BY a.id ASC");
        params[pidx++] = lid_str;
    } else {
        snprintf(hold_sql,
                 sizeof(hold_sql),
                 "SELECT a.id AS asset_id, a.name, c.asset_type, a.currency, "
                 "a.quantity, a.net_value, a.cost_basis "
                 "FROM assets a JOIN categories c ON a.category_id = c.id "
                 "WHERE a.user_id = ? AND c.asset_type IN ('stock','fund','bond','crypto') "
                 "ORDER BY a.id ASC");
    }
    params[pidx] = NULL;

    csilk_json_t* hold_rows =
        csilk_db_query_param_json((csilk_db_pool_t*)db_pool, hold_sql, params);
    if (!hold_rows) {
        return -1;
    }

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
        if (name) {
            snprintf(items[i].name, sizeof(items[i].name), "%s", name);
        }
        const char* atype = csilk_json_get_string(row, "asset_type");
        if (atype) {
            snprintf(items[i].asset_type, sizeof(items[i].asset_type), "%s", atype);
        }
        const char* cur = csilk_json_get_string(row, "currency");
        items[i].currency = currency_from_str(cur ? cur : "CNY");

        items[i].quantity = db_get_quantity(row, "quantity");
        items[i].net_value = db_get_price(row, "net_value", items[i].currency);
        items[i].cost_basis = db_get_money(row, "cost_basis", items[i].currency);
    }

    csilk_json_free(hold_rows);
    *out_items = items;
    *out_count = count;
    return 0;
}

void
mf_portfolio_repo_free_holdings(mf_holding_item_t* items, size_t count)
{
    (void)count;
    if (items) {
        free(items);
    }
}

int
mf_portfolio_repo_get_trade_events(void*                        db_pool,
                                   int64_t                      user_id,
                                   int64_t                      ledger_id,
                                   mf_portfolio_trade_event_t** out_events,
                                   size_t*                      out_count)
{
    if (!db_pool || !out_events || !out_count) {
        return -1;
    }
    *out_events = NULL;
    *out_count = 0;

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* params[4];
    int         pidx = 0;
    params[pidx++] = uid_str;

    char tx_sql[512];
    if (ledger_id > 0) {
        snprintf(tx_sql,
                 sizeof(tx_sql),
                 "SELECT t.id, t.asset_id, t.transaction_type, t.quantity, t.amount, "
                 "t.price_per_unit, t.fee, "
                 "t.currency "
                 "FROM transactions t JOIN assets a ON t.asset_id = a.id "
                 "WHERE t.user_id = ? AND (t.ledger_id = ? OR a.ledger_id = ?) ORDER BY "
                 "t.transaction_date "
                 "ASC, t.id ASC");
        params[pidx++] = lid_str;
        params[pidx++] = lid_str;
    } else {
        snprintf(tx_sql,
                 sizeof(tx_sql),
                 "SELECT id, asset_id, transaction_type, quantity, amount, price_per_unit, fee, "
                 "currency "
                 "FROM transactions WHERE user_id = ? ORDER BY transaction_date ASC, id ASC");
    }
    params[pidx] = NULL;

    csilk_json_t* tx_rows = csilk_db_query_param_json((csilk_db_pool_t*)db_pool, tx_sql, params);
    if (!tx_rows) {
        return -1;
    }

    size_t count = csilk_json_array_size(tx_rows);
    if (count == 0) {
        csilk_json_free(tx_rows);
        return 0;
    }

    mf_portfolio_trade_event_t* events =
        (mf_portfolio_trade_event_t*)calloc(count, sizeof(mf_portfolio_trade_event_t));
    if (!events) {
        csilk_json_free(tx_rows);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        csilk_json_t* row = csilk_json_array_get(tx_rows, i);
        events[i].asset_id = (int64_t)db_get_num(row, "asset_id");
        const char* type = csilk_json_get_string(row, "transaction_type");
        if (type) {
            snprintf(events[i].type, sizeof(events[i].type), "%s", type);
        }
        const char* cur_s = csilk_json_get_string(row, "currency");
        currency_t  cur = currency_from_str(cur_s ? cur_s : "CNY");

        events[i].quantity = db_get_quantity(row, "quantity");
        events[i].amount = db_get_money(row, "amount", cur);
        events[i].price = db_get_price(row, "price_per_unit", cur);
        events[i].fee = db_get_money(row, "fee", cur);
    }

    csilk_json_free(tx_rows);
    *out_events = events;
    *out_count = count;
    return 0;
}

void
mf_portfolio_repo_free_trade_events(mf_portfolio_trade_event_t* events, size_t count)
{
    (void)count;
    if (events) {
        free(events);
    }
}

csilk_json_t*
portfolio_repo_get_category_assets(void* pool, int64_t user_id, int64_t ledger_id)
{
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* params[4];
    int         pidx = 0;
    params[pidx++] = uid_str;

    char sql[512];
    if (ledger_id > 0) {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT c.name as category_name, a.current_value as value, a.currency as currency "
                 "FROM assets a JOIN categories c ON a.category_id=c.id "
                 "WHERE a.user_id=? AND a.ledger_id=? AND c.asset_type NOT IN "
                 "('loan','credit_card','other_liability')");
        params[pidx++] = lid_str;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT c.name as category_name, a.current_value as value, a.currency as currency "
                 "FROM assets a JOIN categories c ON a.category_id=c.id "
                 "WHERE a.user_id=? AND c.asset_type NOT IN "
                 "('loan','credit_card','other_liability')");
    }
    params[pidx] = NULL;

    return csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
}

double
portfolio_repo_get_total_liabilities(void* pool, int64_t user_id, int64_t ledger_id)
{
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* params[4];
    int         pidx = 0;
    params[pidx++] = uid_str;

    char sql[512];
    if (ledger_id > 0) {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT a.current_value, a.currency "
                 "FROM assets a JOIN categories c ON a.category_id=c.id "
                 "WHERE a.user_id=? AND a.ledger_id=? AND c.asset_type IN "
                 "('loan','credit_card','other_liability')");
        params[pidx++] = lid_str;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT a.current_value, a.currency "
                 "FROM assets a JOIN categories c ON a.category_id=c.id "
                 "WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability')");
    }
    params[pidx] = NULL;

    csilk_json_t* liab_rows = csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
    double        total = 0.0;
    if (liab_rows) {
        size_t n = csilk_json_array_size(liab_rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(liab_rows, i);
            const char*   cur = csilk_json_get_string(row, "currency");
            double        v = db_get_num(row, "current_value") * exchange_rate_get_to_cny(cur);
            total += v;
        }
        csilk_json_free(liab_rows);
    }
    return total;
}

csilk_json_t*
portfolio_repo_get_recent_transactions(void* pool, int64_t user_id, int64_t ledger_id, int limit)
{
    char uid_str[32], lid_str[32], lim_str[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(lim_str, sizeof(lim_str), "%d", limit > 0 ? limit : 5);

    const char* params[4];
    int         pidx = 0;
    params[pidx++] = uid_str;

    char sql[512];
    if (ledger_id > 0) {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT t.id, t.transaction_date, t.transaction_type, t.amount, t.currency, "
                 "a.name as asset_name, c.name as category_name "
                 "FROM transactions t "
                 "LEFT JOIN assets a ON t.asset_id=a.id "
                 "LEFT JOIN categories c ON t.category_id=c.id "
                 "WHERE t.user_id=? AND (t.ledger_id=? OR a.ledger_id=?) "
                 "ORDER BY t.transaction_date DESC, t.id DESC LIMIT ?");
        params[pidx++] = lid_str;
        params[pidx++] = lid_str;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT t.id, t.transaction_date, t.transaction_type, t.amount, t.currency, "
                 "a.name as asset_name, c.name as category_name "
                 "FROM transactions t "
                 "LEFT JOIN assets a ON t.asset_id=a.id "
                 "LEFT JOIN categories c ON t.category_id=c.id "
                 "WHERE t.user_id=? "
                 "ORDER BY t.transaction_date DESC, t.id DESC LIMIT ?");
    }
    params[pidx++] = lim_str;
    params[pidx] = NULL;

    return csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
}

typedef struct {
    int64_t id;
    double  current_value;
    char    currency[16];
    char    created_date[16];
    int     is_liability;
} trend_asset_t;

typedef struct {
    int64_t asset_id;
    double  delta;
    char    log_date[16];
} trend_log_t;

csilk_json_t*
portfolio_repo_get_trend_30d(void* pool, int64_t user_id, int64_t ledger_id)
{
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    // 1. Generate the last 30 daily date strings (from now - 29 days to today)
    time_t    now = time(NULL);
    struct tm cur_tm = *localtime(&now);
    char      dates[30][16];
    for (int i = 0; i < 30; i++) {
        struct tm tm = cur_tm;
        tm.tm_mday -= (29 - i);
        mktime(&tm);
        strftime(dates[i], sizeof(dates[i]), "%Y-%m-%d", &tm);
    }

    // 2. Query all assets for the user / ledger
    const char* asset_sql =
        db_is_postgres()
            ? (ledger_id > 0 ? "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "to_char(a.created_at, 'YYYY-MM-DD') as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE a.user_id=? AND a.ledger_id=?"
                             : "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "to_char(a.created_at, 'YYYY-MM-DD') as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE a.user_id=?")
            : (ledger_id > 0 ? "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "date(a.created_at) as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE a.user_id=? AND a.ledger_id=?"
                             : "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "date(a.created_at) as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE a.user_id=?");
    const char*   asset_params[3] = {uid_str, (ledger_id > 0 ? lid_str : NULL), NULL};
    csilk_json_t* asset_rows =
        csilk_db_query_param_json((csilk_db_pool_t*)pool, asset_sql, asset_params);

    size_t         num_assets = asset_rows ? csilk_json_array_size(asset_rows) : 0;
    trend_asset_t* assets =
        num_assets > 0 ? (trend_asset_t*)calloc(num_assets, sizeof(trend_asset_t)) : NULL;
    for (size_t i = 0; i < num_assets; i++) {
        csilk_json_t* r = csilk_json_array_get(asset_rows, i);
        assets[i].id = db_get_int(r, "id");
        assets[i].current_value = db_get_num(r, "current_value");
        const char* cur = csilk_json_get_string(r, "currency");
        snprintf(assets[i].currency, sizeof(assets[i].currency), "%s", cur ? cur : "CNY");
        const char* cd = csilk_json_get_string(r, "created_date");
        snprintf(
            assets[i].created_date, sizeof(assets[i].created_date), "%s", cd ? cd : "1970-01-01");
        const char* atype = csilk_json_get_string(r, "asset_type");
        assets[i].is_liability =
            (atype && (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                       strcmp(atype, "other_liability") == 0));
    }
    if (asset_rows) {
        csilk_json_free(asset_rows);
    }

    // 3. Query all balance delta logs occurred on or after dates[0]
    const char* log_sql =
        db_is_postgres()
            ? (ledger_id > 0
                   ? "SELECT abl.asset_id, abl.delta, to_char(abl.created_at, 'YYYY-MM-DD') as "
                     "log_date "
                     "FROM asset_balance_logs abl JOIN assets a ON abl.asset_id=a.id "
                     "WHERE abl.user_id=? AND a.ledger_id=? AND abl.created_at::date >= ?::date"
                   : "SELECT abl.asset_id, abl.delta, to_char(abl.created_at, 'YYYY-MM-DD') as "
                     "log_date "
                     "FROM asset_balance_logs abl "
                     "WHERE abl.user_id=? AND abl.created_at::date >= ?::date")
            : (ledger_id > 0 ? "SELECT abl.asset_id, abl.delta, date(abl.created_at) as log_date "
                               "FROM asset_balance_logs abl JOIN assets a ON abl.asset_id=a.id "
                               "WHERE abl.user_id=? AND a.ledger_id=? AND date(abl.created_at) >= ?"
                             : "SELECT abl.asset_id, abl.delta, date(abl.created_at) as log_date "
                               "FROM asset_balance_logs abl "
                               "WHERE abl.user_id=? AND date(abl.created_at) >= ?");
    const char* log_params[4] = {
        uid_str, (ledger_id > 0 ? lid_str : dates[0]), (ledger_id > 0 ? dates[0] : NULL), NULL};
    csilk_json_t* log_rows = csilk_db_query_param_json((csilk_db_pool_t*)pool, log_sql, log_params);

    size_t       num_logs = log_rows ? csilk_json_array_size(log_rows) : 0;
    trend_log_t* logs = num_logs > 0 ? (trend_log_t*)calloc(num_logs, sizeof(trend_log_t)) : NULL;
    for (size_t i = 0; i < num_logs; i++) {
        csilk_json_t* r = csilk_json_array_get(log_rows, i);
        logs[i].asset_id = db_get_int(r, "asset_id");
        logs[i].delta = db_get_num(r, "delta");
        const char* ld = csilk_json_get_string(r, "log_date");
        snprintf(logs[i].log_date, sizeof(logs[i].log_date), "%s", ld ? ld : "");
    }
    if (log_rows) {
        csilk_json_free(log_rows);
    }

    // 4. Reconstruct historical balances for each day by rolling back deltas
    double day_assets[30] = {0};
    double day_liabs[30] = {0};

    for (int d = 0; d < 30; d++) {
        const char* target_date = dates[d];
        for (size_t a = 0; a < num_assets; a++) {
            if (strcmp(assets[a].created_date, target_date) > 0) {
                // Asset did not exist yet on target_date
                continue;
            }
            double val = assets[a].current_value;
            for (size_t l = 0; l < num_logs; l++) {
                if (logs[l].asset_id == assets[a].id && strcmp(logs[l].log_date, target_date) > 0) {
                    val -= logs[l].delta;
                }
            }
            double conv_val = val * exchange_rate_get_to_cny(assets[a].currency);
            if (assets[a].is_liability) {
                day_liabs[d] += conv_val;
            } else {
                day_assets[d] += conv_val;
            }
        }
    }

    if (assets) {
        free(assets);
    }
    if (logs) {
        free(logs);
    }

    // 5. Serialize into [{date, total_assets, total_liabilities, net_worth}, ...]
    csilk_json_t* trend_arr = csilk_json_array();
    for (int d = 0; d < 30; d++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "date", dates[d]);
        csilk_json_add_number(item, "total_assets", day_assets[d]);
        csilk_json_add_number(item, "total_liabilities", day_liabs[d]);
        csilk_json_add_number(item, "net_worth", day_assets[d] - day_liabs[d]);
        csilk_json_array_append(trend_arr, item);
    }

    char* trend_str = csilk_json_serialize(trend_arr, NULL);
    csilk_json_free(trend_arr);

    csilk_json_t* row = csilk_json_object();
    csilk_json_add_string(row, "trend", trend_str ? trend_str : "[]");
    if (trend_str) {
        free(trend_str);
    }

    csilk_json_t* result = csilk_json_array();
    csilk_json_array_append(result, row);
    return result;
}

csilk_json_t*
portfolio_repo_get_performance_transactions(void* pool, int64_t user_id, int64_t ledger_id)
{
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* params[4];
    int         pidx = 0;
    params[pidx++] = uid_str;

    char sql[512];
    if (ledger_id > 0) {
        snprintf(
            sql,
            sizeof(sql),
            "SELECT t.id, t.asset_id, a.name as asset_name, t.transaction_type, t.direction, "
            "t.transaction_date, "
            "t.quantity, t.price_per_unit, t.amount, t.fee, t.currency, t.parent_tx_id "
            "FROM transactions t JOIN assets a ON t.asset_id=a.id "
            "WHERE t.user_id=? AND (t.ledger_id=? OR a.ledger_id=?) AND (t.parent_tx_id IS NULL OR "
            "t.parent_tx_id = 0) "
            "ORDER BY t.transaction_date ASC, t.id ASC");
        params[pidx++] = lid_str;
        params[pidx++] = lid_str;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT t.id, t.asset_id, a.name as asset_name, t.transaction_type, t.direction, "
                 "t.transaction_date, "
                 "t.quantity, t.price_per_unit, t.amount, t.fee, t.currency, t.parent_tx_id "
                 "FROM transactions t JOIN assets a ON t.asset_id=a.id "
                 "WHERE t.user_id=? AND (t.parent_tx_id IS NULL OR t.parent_tx_id = 0) "
                 "ORDER BY t.transaction_date ASC, t.id ASC");
    }
    params[pidx] = NULL;

    return csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
}

int
portfolio_repo_get_current_holdings_totals(void*   pool,
                                           int64_t user_id,
                                           int64_t ledger_id,
                                           double* out_qty,
                                           double* out_cost,
                                           double* out_market)
{
    if (!out_qty || !out_cost || !out_market) {
        return -1;
    }
    *out_qty = 0.0;
    *out_cost = 0.0;
    *out_market = 0.0;

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* params[4];
    int         pidx = 0;
    params[pidx++] = uid_str;

    char sql[512];
    if (ledger_id > 0) {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT a.quantity, a.cost_basis, a.current_value, a.currency "
                 "FROM assets a JOIN categories c ON a.category_id=c.id "
                 "WHERE a.user_id=? AND a.ledger_id=? AND c.asset_type IN "
                 "('stock','fund','bond','crypto')");
        params[pidx++] = lid_str;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT a.quantity, a.cost_basis, a.current_value, a.currency "
                 "FROM assets a JOIN categories c ON a.category_id=c.id "
                 "WHERE a.user_id=? AND c.asset_type IN ('stock','fund','bond','crypto')");
    }
    params[pidx] = NULL;

    csilk_json_t* pos_rows = csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);

    if (pos_rows) {
        size_t count = csilk_json_array_size(pos_rows);
        double total_qty = 0.0;
        double total_cost = 0.0;
        double total_market = 0.0;

        for (size_t i = 0; i < count; i++) {
            const csilk_json_t* row = csilk_json_array_get(pos_rows, i);
            double              q = db_get_num(row, "quantity");
            double              c = db_get_num(row, "cost_basis");
            double              m = db_get_num(row, "current_value");
            const char*         cur = csilk_json_get_string(row, "currency");
            double              fx = exchange_rate_get_to_cny(cur);

            total_qty += q;
            total_cost += (c * fx);
            total_market += (m * fx);
        }

        *out_market = total_market;
        *out_cost = total_cost;
        *out_qty = total_qty;
        csilk_json_free(pos_rows);
        return 0;
    }
    return -1;
}
