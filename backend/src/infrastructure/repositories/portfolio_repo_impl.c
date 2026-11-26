#include "infrastructure/repositories/portfolio_repo_impl.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    const char* params[12];
    int         pidx = 0;

    char hold_sql[2048];
    if (ledger_id > 0) {
        snprintf(hold_sql,
                 sizeof(hold_sql),
                 "SELECT a.id AS asset_id, a.name, c.asset_type, a.currency, "
                 "a.quantity, a.net_value, a.cost_basis, a.cost_method "
                 "FROM assets a JOIN categories c ON a.category_id = c.id "
                 "WHERE " MF_LEDGER_SHARED_ACCESS_SQL(
                     "a") " AND c.asset_type IN "
                          "('stock','fund','bond','crypto') AND " MF_RBAC_ASSET_UNMASKED_SQL(
                              "a") " "
                                   "ORDER BY a.id ASC");
        params[pidx++] = lid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
    } else {
        snprintf(hold_sql,
                 sizeof(hold_sql),
                 "SELECT a.id AS asset_id, a.name, c.asset_type, a.currency, "
                 "a.quantity, a.net_value, a.cost_basis, a.cost_method "
                 "FROM assets a JOIN categories c ON a.category_id = c.id "
                 "WHERE a.user_id = ? AND c.asset_type IN ('stock','fund','bond','crypto') "
                 "AND " MF_RBAC_ASSET_UNMASKED_SQL("a") " "
                                                        "ORDER BY a.id ASC");
        params[pidx++] = uid_str;
    }
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
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
        const char* cm = csilk_json_get_string(row, "cost_method");
        snprintf(items[i].cost_method,
                 sizeof(items[i].cost_method),
                 "%s",
                 (cm && cm[0]) ? cm : "weighted_average");
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

    const char* params[12];
    int         pidx = 0;

    char tx_sql[2048];
    if (ledger_id > 0) {
        snprintf(tx_sql,
                 sizeof(tx_sql),
                 "SELECT t.id, t.asset_id, t.transaction_type, t.quantity, t.amount, "
                 "t.price_per_unit, t.fee, "
                 "t.currency "
                 "FROM transactions t JOIN assets a ON t.asset_id = a.id "
                 "WHERE " MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL(
                     "t", "a") " "
                               "AND (t.parent_tx_id IS NULL OR t.parent_tx_id = 0) "
                               "AND " MF_RBAC_ASSET_UNMASKED_SQL(
                                   "a") " "
                                        "ORDER BY t.transaction_date ASC, t.id ASC");
        params[pidx++] = lid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
    } else {
        snprintf(
            tx_sql,
            sizeof(tx_sql),
            "SELECT t.id, t.asset_id, t.transaction_type, t.quantity, t.amount, "
            "t.price_per_unit, t.fee, "
            "t.currency "
            "FROM transactions t LEFT JOIN assets a ON t.asset_id = a.id "
            "WHERE t.user_id = ? "
            "AND (t.parent_tx_id IS NULL OR t.parent_tx_id = 0) "
            "AND " MF_RBAC_ASSET_UNMASKED_SQL("a") " "
                                                   "ORDER BY t.transaction_date ASC, t.id ASC");
        params[pidx++] = uid_str;
    }
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
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

    const char* params[12];
    int         pidx = 0;

    char sql[2048];
    if (ledger_id > 0) {
        snprintf(
            sql,
            sizeof(sql),
            "SELECT c.name as category_name, a.current_value as value, a.currency as currency "
            "FROM assets a JOIN categories c ON a.category_id=c.id "
            "WHERE " MF_LEDGER_SHARED_ACCESS_SQL(
                "a") " AND c.asset_type NOT IN "
                     "('loan','credit_card','other_liability') AND " MF_RBAC_ASSET_UNMASKED_SQL(
                         "a"));
        params[pidx++] = lid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT c.name as category_name, a.current_value as value, a.currency as currency "
                 "FROM assets a JOIN categories c ON a.category_id=c.id "
                 "WHERE a.user_id=? AND c.asset_type NOT IN "
                 "('loan','credit_card','other_liability') AND " MF_RBAC_ASSET_UNMASKED_SQL("a"));
        params[pidx++] = uid_str;
    }
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
    params[pidx] = NULL;

    return csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
}

csilk_json_t*
portfolio_repo_get_liability_assets(void* pool, int64_t user_id, int64_t ledger_id)
{
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* params[12];
    int         pidx = 0;

    char sql[2048];
    if (ledger_id > 0) {
        snprintf(
            sql,
            sizeof(sql),
            "SELECT a.current_value, a.currency "
            "FROM assets a JOIN categories c ON a.category_id=c.id "
            "WHERE " MF_LEDGER_SHARED_ACCESS_SQL(
                "a") " AND c.asset_type IN "
                     "('loan','credit_card','other_liability') AND " MF_RBAC_ASSET_UNMASKED_SQL(
                         "a"));
        params[pidx++] = lid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT a.current_value, a.currency "
                 "FROM assets a JOIN categories c ON a.category_id=c.id "
                 "WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability') "
                 "AND " MF_RBAC_ASSET_UNMASKED_SQL("a"));
        params[pidx++] = uid_str;
    }
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
    params[pidx] = NULL;

    return csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
}

static csilk_json_t*
format_recent_tx_rows(csilk_json_t* raw_rows)
{
    if (!raw_rows || !csilk_json_is_array(raw_rows)) {
        return raw_rows;
    }
    size_t        n = csilk_json_array_size(raw_rows);
    csilk_json_t* out = csilk_json_array();
    if (!out) {
        return raw_rows;
    }
    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* r = csilk_json_array_get(raw_rows, i);
        if (!r || !csilk_json_is_object(r)) {
            continue;
        }
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_int(item, "id", db_get_int(r, "id"));
        csilk_json_add_string(
            item, "transaction_date", csilk_json_get_string(r, "transaction_date"));
        csilk_json_add_string(
            item, "transaction_type", csilk_json_get_string(r, "transaction_type"));

        int mask_bal =
            (db_get_int(r, "mask_bal_a") != 0 || db_get_int(r, "mask_bal_la") != 0) ? 1 : 0;
        if (mask_bal) {
            csilk_json_add_number(item, "amount", 0.0);
            csilk_json_add_bool(item, "is_balance_masked", true);
        } else {
            csilk_json_add_number(item, "amount", db_get_num(r, "amount"));
            csilk_json_add_bool(item, "is_balance_masked", false);
        }
        csilk_json_add_string(item, "currency", csilk_json_get_string(r, "currency"));
        csilk_json_add_string(item, "asset_name", csilk_json_get_string(r, "asset_name"));
        csilk_json_add_string(item, "category_name", csilk_json_get_string(r, "category_name"));
        csilk_json_array_append(out, item);
    }
    csilk_json_free(raw_rows);
    return out;
}

csilk_json_t*
portfolio_repo_get_recent_transactions(void* pool, int64_t user_id, int64_t ledger_id, int limit)
{
    char uid_str[32], lid_str[32], lim_str[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    snprintf(lim_str, sizeof(lim_str), "%d", limit > 0 ? limit : 5);

    const char* params[24];
    int         pidx = 0;
    /* SELECT 中 2 个脱敏列共 4 个 uid 参数 */
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;

    char sql[4096];
    if (ledger_id > 0) {
        snprintf(
            sql,
            sizeof(sql),
            "SELECT t.id, t.transaction_date, t.transaction_type, t.amount, t.currency, "
            "a.name as asset_name, c.name as category_name, " MF_RBAC_MASK_BALANCE_COL(
                "a") " AS mask_bal_a, " MF_RBAC_MASK_BALANCE_COL("la") " AS mask_bal_la "
                                                                       "FROM transactions t "
                                                                       "LEFT JOIN assets a ON "
                                                                       "t.asset_id=a.id "
                                                                       "LEFT JOIN assets la ON "
                                                                       "t.linked_asset_id=la.id "
                                                                       "LEFT JOIN categories c ON "
                                                                       "t.category_id=c.id "
                                                                       "WHERE"
                                                                       " " MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL(
                                                                           "t",
                                                                           "a") " "
                                                                                "AND"
                                                                                " " MF_RBAC_ASSET_VISIBLE_SQL(
                                                                                    "a") " "
                                                                                         "AND"
                                                                                         " " MF_RBAC_ASSET_VISIBLE_SQL(
                                                                                             "la") " "
                                                                                                   "ORDER BY t.transaction_date DESC, t.id DESC LIMIT ?");
        params[pidx++] = lid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
    } else {
        snprintf(
            sql,
            sizeof(sql),
            "SELECT t.id, t.transaction_date, t.transaction_type, t.amount, t.currency, "
            "a.name as asset_name, c.name as category_name, " MF_RBAC_MASK_BALANCE_COL(
                "a") " AS mask_bal_a, " MF_RBAC_MASK_BALANCE_COL("la") " AS mask_bal_la "
                                                                       "FROM transactions t "
                                                                       "LEFT JOIN assets a ON "
                                                                       "t.asset_id=a.id "
                                                                       "LEFT JOIN assets la ON "
                                                                       "t.linked_asset_id=la.id "
                                                                       "LEFT JOIN categories c ON "
                                                                       "t.category_id=c.id "
                                                                       "WHERE t.user_id=? "
                                                                       "AND"
                                                                       " " MF_RBAC_ASSET_VISIBLE_SQL(
                                                                           "a") " "
                                                                                "AND"
                                                                                " " MF_RBAC_ASSET_VISIBLE_SQL(
                                                                                    "la") " "
                                                                                          "ORDER "
                                                                                          "BY "
                                                                                          "t."
                                                                                          "transact"
                                                                                          "ion_"
                                                                                          "date "
                                                                                          "DESC, "
                                                                                          "t.id "
                                                                                          "DESC "
                                                                                          "LIMIT "
                                                                                          "?");
        params[pidx++] = uid_str;
    }
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
    params[pidx++] = lim_str;
    params[pidx] = NULL;

    csilk_json_t* raw_rows = csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
    return raw_rows ? format_recent_tx_rows(raw_rows) : NULL;
}

csilk_json_t*
portfolio_repo_get_trend_assets(void* pool, int64_t user_id, int64_t ledger_id)
{
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* asset_sql =
        db_is_postgres()
            ? (ledger_id > 0 ? "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "to_char(a.created_at, 'YYYY-MM-DD') as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE " MF_LEDGER_SHARED_ACCESS_SQL(
                                   "a") " AND " MF_RBAC_ASSET_UNMASKED_SQL("a")
                             : "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "to_char(a.created_at, 'YYYY-MM-DD') as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE a.user_id=? AND " MF_RBAC_ASSET_UNMASKED_SQL("a"))
            : (ledger_id > 0 ? "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "date(a.created_at) as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE " MF_LEDGER_SHARED_ACCESS_SQL(
                                   "a") " AND " MF_RBAC_ASSET_UNMASKED_SQL("a")
                             : "SELECT a.id, a.current_value, a.currency, c.asset_type, "
                               "date(a.created_at) as created_date "
                               "FROM assets a JOIN categories c ON a.category_id=c.id "
                               "WHERE a.user_id=? AND " MF_RBAC_ASSET_UNMASKED_SQL("a"));
    const char* asset_params[10];
    int         apidx = 0;
    if (ledger_id > 0) {
        asset_params[apidx++] = lid_str;
        asset_params[apidx++] = uid_str;
        asset_params[apidx++] = uid_str;
        asset_params[apidx++] = uid_str;
    } else {
        asset_params[apidx++] = uid_str;
    }
    asset_params[apidx++] = uid_str;
    asset_params[apidx++] = uid_str;
    asset_params[apidx] = NULL;
    return csilk_db_query_param_json((csilk_db_pool_t*)pool, asset_sql, asset_params);
}

csilk_json_t*
portfolio_repo_get_trend_balance_logs(void*       pool,
                                      int64_t     user_id,
                                      int64_t     ledger_id,
                                      const char* start_date)
{
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* log_sql =
        db_is_postgres()
            ? (ledger_id > 0
                   ? "SELECT abl.asset_id, abl.delta, to_char(abl.created_at, 'YYYY-MM-DD') as "
                     "log_date "
                     "FROM asset_balance_logs abl JOIN assets a ON abl.asset_id=a.id "
                     "WHERE " MF_LEDGER_SHARED_BALANCE_LOG_SQL(
                         "abl",
                         "a") " "
                              "AND abl.created_at::date >= ?::date AND " MF_RBAC_ASSET_UNMASKED_SQL(
                                  "a")
                   : "SELECT abl.asset_id, abl.delta, to_char(abl.created_at, 'YYYY-MM-DD') as "
                     "log_date "
                     "FROM asset_balance_logs abl LEFT JOIN assets a ON abl.asset_id=a.id "
                     "WHERE abl.user_id=? AND abl.created_at::date >= ?::date "
                     "AND " MF_RBAC_ASSET_UNMASKED_SQL("a"))
            : (ledger_id > 0
                   ? "SELECT abl.asset_id, abl.delta, date(abl.created_at) as log_date "
                     "FROM asset_balance_logs abl JOIN assets a ON abl.asset_id=a.id "
                     "WHERE " MF_LEDGER_SHARED_BALANCE_LOG_SQL(
                         "abl",
                         "a") " "
                              "AND date(abl.created_at) >= ? AND " MF_RBAC_ASSET_UNMASKED_SQL("a")
                   : "SELECT abl.asset_id, abl.delta, date(abl.created_at) as log_date "
                     "FROM asset_balance_logs abl LEFT JOIN assets a ON abl.asset_id=a.id "
                     "WHERE abl.user_id=? AND date(abl.created_at) >= ? "
                     "AND " MF_RBAC_ASSET_UNMASKED_SQL("a"));
    const char* log_params[12];
    int         lpidx = 0;
    if (ledger_id > 0) {
        log_params[lpidx++] = lid_str;
        log_params[lpidx++] = uid_str;
        log_params[lpidx++] = uid_str;
        log_params[lpidx++] = uid_str;
    } else {
        log_params[lpidx++] = uid_str;
    }
    log_params[lpidx++] = start_date;
    log_params[lpidx++] = uid_str;
    log_params[lpidx++] = uid_str;
    log_params[lpidx] = NULL;
    return csilk_db_query_param_json((csilk_db_pool_t*)pool, log_sql, log_params);
}

csilk_json_t*
portfolio_repo_get_performance_transactions(void* pool, int64_t user_id, int64_t ledger_id)
{
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* params[12];
    int         pidx = 0;

    char sql[2048];
    if (ledger_id > 0) {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT t.id, t.asset_id, a.name as asset_name, t.transaction_type, t.direction, "
                 "t.transaction_date, "
                 "t.quantity, t.price_per_unit, t.amount, t.fee, t.currency, t.parent_tx_id "
                 "FROM transactions t JOIN assets a ON t.asset_id=a.id "
                 "WHERE " MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL(
                     "t", "a") " "
                               "AND (t.parent_tx_id IS NULL OR t.parent_tx_id = 0) "
                               "AND " MF_RBAC_ASSET_UNMASKED_SQL(
                                   "a") " "
                                        "ORDER BY t.transaction_date ASC, t.id ASC");
        params[pidx++] = lid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
    } else {
        snprintf(
            sql,
            sizeof(sql),
            "SELECT t.id, t.asset_id, a.name as asset_name, t.transaction_type, t.direction, "
            "t.transaction_date, "
            "t.quantity, t.price_per_unit, t.amount, t.fee, t.currency, t.parent_tx_id "
            "FROM transactions t JOIN assets a ON t.asset_id=a.id "
            "WHERE t.user_id=? AND (t.parent_tx_id IS NULL OR t.parent_tx_id = 0) "
            "AND " MF_RBAC_ASSET_UNMASKED_SQL("a") " "
                                                   "ORDER BY t.transaction_date ASC, t.id ASC");
        params[pidx++] = uid_str;
    }
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
    params[pidx] = NULL;

    return csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
}

csilk_json_t*
portfolio_repo_get_holdings_raw(void* pool, int64_t user_id, int64_t ledger_id)
{
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* params[12];
    int         pidx = 0;

    char sql[2048];
    if (ledger_id > 0) {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT a.cost_basis, a.current_value, a.currency "
                 "FROM assets a JOIN categories c ON a.category_id=c.id "
                 "WHERE " MF_LEDGER_SHARED_ACCESS_SQL(
                     "a") " AND c.asset_type IN "
                          "('stock','fund','bond','crypto') AND " MF_RBAC_ASSET_UNMASKED_SQL("a"));
        params[pidx++] = lid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
        params[pidx++] = uid_str;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "SELECT a.cost_basis, a.current_value, a.currency "
                 "FROM assets a JOIN categories c ON a.category_id=c.id "
                 "WHERE a.user_id=? AND c.asset_type IN ('stock','fund','bond','crypto') "
                 "AND " MF_RBAC_ASSET_UNMASKED_SQL("a"));
        params[pidx++] = uid_str;
    }
    params[pidx++] = uid_str;
    params[pidx++] = uid_str;
    params[pidx] = NULL;

    return csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
}
