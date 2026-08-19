#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void report_expense_monthly(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }
    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");
    char year_buf[8] = {0}, month_buf[4] = {0};
    if (!year_str || !month_str) {
        time_t now = time(NULL);
        struct tm* tm_now = localtime(&now);
        strftime(year_buf, sizeof(year_buf), "%Y", tm_now);
        strftime(month_buf, sizeof(month_buf), "%m", tm_now);
        year_str = year_buf;
        month_str = month_buf;
    }
    char date_pattern[32];
    snprintf(date_pattern, sizeof(date_pattern), "%s-%02d-%%", year_str, atoi(month_str));

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, date_pattern, NULL };

    csilk_db_pool_t* pool = db_get_pool();

    csilk_json_t* totals = csilk_db_query_param_json(pool,
        "SELECT COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as total_income, "
        "COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as total_expense "
        "FROM daily_expenses WHERE user_id=? AND expense_date LIKE ?", params);
    double income = 0, expense = 0;
    if (totals && csilk_json_array_size(totals) > 0) {
        income = db_get_num(csilk_json_array_get(totals, 0), "total_income");
        expense = db_get_num(csilk_json_array_get(totals, 0), "total_expense");
    }
    if (totals) csilk_json_free(totals);

    csilk_json_t* by_cat = csilk_db_query_param_json(pool,
        "SELECT c.name as name, de.expense_type, SUM(de.amount) as amount "
        "FROM daily_expenses de JOIN categories c ON de.category_id=c.id "
        "WHERE de.user_id=? AND de.expense_date LIKE ? "
        "GROUP BY c.name, de.expense_type ORDER BY amount DESC", params);

    csilk_json_t* by_tag = csilk_db_query_param_json(pool,
        "SELECT t.name as tag_name, SUM(de.amount) as amount, COUNT(*) as count "
        "FROM daily_expenses de JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id "
        "WHERE de.user_id=? AND de.expense_date LIKE ? "
        "GROUP BY t.name ORDER BY amount DESC", params);

    csilk_json_t* daily = csilk_db_query_param_json(pool,
        "SELECT expense_date, "
        "COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as income, "
        "COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as expense "
        "FROM daily_expenses WHERE user_id=? AND expense_date LIKE ? "
        "GROUP BY expense_date ORDER BY expense_date", params);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "year", atoll(year_str));
    csilk_json_add_number(resp, "month", atoll(month_str));
    csilk_json_add_number(resp, "total_income", income);
    csilk_json_add_number(resp, "total_expense", expense);
    csilk_json_add_number(resp, "balance", income - expense);
    csilk_json_add_array(resp, "by_category", by_cat ? by_cat : csilk_json_array());
    csilk_json_add_array(resp, "by_tag", by_tag ? by_tag : csilk_json_array());
    csilk_json_add_array(resp, "daily_breakdown", daily ? daily : csilk_json_array());
    respond_ok(c, resp);
}

void report_expense_trend(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }
    const char* months_str = csilk_get_query(c, "months");
    int months = months_str ? atoi(months_str) : 6;
    if (months <= 0 || months > 24) months = 6;

    char uid_str[32], months_buf[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(months_buf, sizeof(months_buf), "%d", months);
    const char* params[] = { uid_str, months_buf, NULL };

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* result = csilk_db_query_param_json(pool,
        "SELECT SUBSTR(expense_date,1,7) as period, "
        "COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as income, "
        "COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as expense "
        "FROM daily_expenses WHERE user_id=? AND expense_date >= date('now','-'||?||' months') "
        "GROUP BY SUBSTR(expense_date,1,7) ORDER BY period", params);

    if (!result) { respond_error(c, 500, "查询失败"); return; }
    csilk_json_t* resp = csilk_json_object();
    csilk_json_t* labels = csilk_json_array();
    csilk_json_t* income_arr = csilk_json_array();
    csilk_json_t* expense_arr = csilk_json_array();
    size_t n = csilk_json_array_size(result);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(result, i);
        csilk_json_array_append(labels, csilk_json_string_new(csilk_json_get_string(row, "period")));
        csilk_json_array_append(income_arr, csilk_json_number(db_get_num(row, "income")));
        csilk_json_array_append(expense_arr, csilk_json_number(db_get_num(row, "expense")));
    }
    csilk_json_add_array(resp, "labels", labels);
    csilk_json_add_array(resp, "income", income_arr);
    csilk_json_add_array(resp, "expense", expense_arr);
    csilk_json_free(result);
    respond_ok(c, resp);
}

/** @brief GET /api/reports/expense/yearly — 自然年按月收支（1-12月零补齐）*/
void report_expense_yearly(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* year_str = csilk_get_query(c, "year");
    char year_buf[8] = {0};
    if (year_str && strlen(year_str) > 0) {
        snprintf(year_buf, sizeof(year_buf), "%s", year_str);
    } else {
        time_t now = time(NULL);
        strftime(year_buf, sizeof(year_buf), "%Y", localtime(&now));
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, year_buf, NULL };

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* rows = csilk_db_query_param_json(pool,
        "SELECT CAST(SUBSTR(expense_date,6,2) AS INTEGER) as m, "
        "COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as income, "
        "COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as expense "
        "FROM daily_expenses WHERE user_id=? AND SUBSTR(expense_date,1,4)=? "
        "GROUP BY m ORDER BY m", params);
    if (!rows) { respond_error(c, 500, "查询失败"); return; }

    /* 零补齐 1-12 月 */
    double income_by_month[13] = {0};
    double expense_by_month[13] = {0};
    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        int m = (int)db_get_num(csilk_json_array_get(rows, i), "m");
        if (m >= 1 && m <= 12) {
            income_by_month[m] = db_get_num(csilk_json_array_get(rows, i), "income");
            expense_by_month[m] = db_get_num(csilk_json_array_get(rows, i), "expense");
        }
    }
    csilk_json_free(rows);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_t* labels = csilk_json_array();
    csilk_json_t* income_arr = csilk_json_array();
    csilk_json_t* expense_arr = csilk_json_array();
    for (int m = 1; m <= 12; m++) {
        char label[16];
        snprintf(label, sizeof(label), "%d月", m);
        csilk_json_array_append(labels, csilk_json_string_new(label));
        csilk_json_array_append(income_arr, csilk_json_number(income_by_month[m]));
        csilk_json_array_append(expense_arr, csilk_json_number(expense_by_month[m]));
    }
    csilk_json_add_number(resp, "year", atoll(year_buf));
    csilk_json_add_array(resp, "labels", labels);
    csilk_json_add_array(resp, "income", income_arr);
    csilk_json_add_array(resp, "expense", expense_arr);
    respond_ok(c, resp);
}

void report_expense_category(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }
    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");
    char year_buf[8] = {0};
    time_t now = time(NULL);
    strftime(year_buf, sizeof(year_buf), "%Y", localtime(&now));
    char period[16], period_pattern[32];
    if (year_str && month_str) {
        snprintf(period, sizeof(period), "%s-%02d-", year_str, atoi(month_str));
    } else {
        snprintf(period, sizeof(period), "%s-", year_buf);
    }
    snprintf(period_pattern, sizeof(period_pattern), "%s%%", period);

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, period_pattern, NULL };

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* rows = csilk_db_query_param_json(pool,
        "SELECT c.name as name, SUM(de.amount) as amount "
        "FROM daily_expenses de JOIN categories c ON de.category_id=c.id "
        "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date LIKE ? "
        "GROUP BY c.name ORDER BY amount DESC", params);
    if (!rows) { respond_error(c, 500, "查询失败"); return; }
    double total = 0;
    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) total += db_get_num(csilk_json_array_get(rows, i), "amount");
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", period);
    csilk_json_t* items = csilk_json_array();
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        double amt = db_get_num(row, "amount");
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", csilk_json_get_string(row, "name"));
        csilk_json_add_number(item, "amount", amt);
        csilk_json_add_number(item, "pct", total > 0 ? (amt / total * 100) : 0);
        csilk_json_array_append(items, item);
    }
    csilk_json_add_array(resp, "items", items);
    csilk_json_free(rows);
    respond_ok(c, resp);
}

void report_expense_tag(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }
    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");
    char year_buf[8] = {0};
    time_t now = time(NULL);
    strftime(year_buf, sizeof(year_buf), "%Y", localtime(&now));
    char period[16], period_pattern[32];
    if (year_str && month_str) {
        snprintf(period, sizeof(period), "%s-%02d-", year_str, atoi(month_str));
    } else {
        snprintf(period, sizeof(period), "%s-", year_buf);
    }
    snprintf(period_pattern, sizeof(period_pattern), "%s%%", period);

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, period_pattern, NULL };

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* rows = csilk_db_query_param_json(pool,
        "SELECT t.name as tag_name, SUM(de.amount) as amount, COUNT(*) as count "
        "FROM daily_expenses de JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id "
        "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date LIKE ? "
        "GROUP BY t.name ORDER BY amount DESC", params);
    if (!rows) { respond_error(c, 500, "查询失败"); return; }
    double total = 0;
    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) total += db_get_num(csilk_json_array_get(rows, i), "amount");
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", period);
    csilk_json_t* items = csilk_json_array();
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        double amt = db_get_num(row, "amount");
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "tag_name", csilk_json_get_string(row, "tag_name"));
        csilk_json_add_number(item, "amount", amt);
        csilk_json_add_number(item, "count", db_get_num(row, "count"));
        csilk_json_add_number(item, "pct", total > 0 ? (amt / total * 100) : 0);
        csilk_json_array_append(items, item);
    }
    csilk_json_add_array(resp, "items", items);
    csilk_json_free(rows);
    respond_ok(c, resp);
}

void report_asset_trend(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }
    const char* period_str = csilk_get_query(c, "period");
    int days = 30;
    if (period_str) {
        if (strcmp(period_str, "90d") == 0) days = 90;
        else if (strcmp(period_str, "365d") == 0) days = 365;
        else days = atoi(period_str);
    }
    if (days <= 0 || days > 365) days = 30;

    char days_str[32], uid_str[32];
    snprintf(days_str, sizeof(days_str), "%d", days - 1);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { days_str, uid_str, days_str, uid_str, days_str, NULL };

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* result = csilk_db_query_param_json(pool,
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
        "FROM pts) ORDER BY d", params);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", period_str ? period_str : "30d");
    if (result && csilk_json_array_size(result) > 0) {
        csilk_json_t* row = csilk_json_array_get(result, 0);
        const char* labels_str = csilk_json_get_string(row, "labels");
        const char* nw_str = csilk_json_get_string(row, "net_worth");
        const char* assets_str = csilk_json_get_string(row, "assets");
        const char* liabs_str = csilk_json_get_string(row, "liabilities");
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
    if (result) csilk_json_free(result);
    respond_ok(c, resp);
}

void report_asset_breakdown(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, NULL };

    csilk_json_t* assets = csilk_db_query_param_json(pool,
        "SELECT c.name as name, SUM(a.current_value) as value "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type NOT IN ('loan','credit_card','other_liability') "
        "GROUP BY c.name ORDER BY value DESC", params);
    csilk_json_t* liabs = csilk_db_query_param_json(pool,
        "SELECT c.name as name, SUM(a.current_value) as value "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability') "
        "GROUP BY c.name ORDER BY value DESC", params);
    double total_assets = 0, total_liabs = 0;
    if (assets) {
        size_t n = csilk_json_array_size(assets);
        for (size_t i = 0; i < n; i++) total_assets += db_get_num(csilk_json_array_get(assets, i), "value");
    }
    if (liabs) {
        size_t n = csilk_json_array_size(liabs);
        for (size_t i = 0; i < n; i++) total_liabs += db_get_num(csilk_json_array_get(liabs, i), "value");
    }
    csilk_json_t* resp = csilk_json_object();
    csilk_json_t* asset_items = csilk_json_array();
    if (assets) {
        size_t n = csilk_json_array_size(assets);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(assets, i);
            double v = db_get_num(row, "value");
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_string(item, "name", csilk_json_get_string(row, "name"));
            csilk_json_add_number(item, "value", v);
            csilk_json_add_number(item, "pct", total_assets > 0 ? (v / total_assets * 100) : 0);
            csilk_json_array_append(asset_items, item);
        }
        csilk_json_free(assets);
    }
    csilk_json_add_array(resp, "assets", asset_items);
    csilk_json_t* liab_items = csilk_json_array();
    if (liabs) {
        size_t n = csilk_json_array_size(liabs);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(liabs, i);
            double v = db_get_num(row, "value");
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_string(item, "name", csilk_json_get_string(row, "name"));
            csilk_json_add_number(item, "value", v);
            csilk_json_add_number(item, "pct", total_liabs > 0 ? (v / total_liabs * 100) : 0);
            csilk_json_array_append(liab_items, item);
        }
        csilk_json_free(liabs);
    }
    csilk_json_add_array(resp, "liabilities", liab_items);
    csilk_json_add_number(resp, "total_assets", total_assets);
    csilk_json_add_number(resp, "total_liabilities", total_liabs);
    csilk_json_add_number(resp, "net_worth", total_assets - total_liabs);
    respond_ok(c, resp);
}

void report_transaction_performance(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, NULL };

    csilk_json_t* result = csilk_db_query_param_json(pool,
        "SELECT t.id, a.name as asset_name, t.transaction_type, t.direction, t.transaction_date, "
        "t.quantity, t.price_per_unit, t.amount, t.fee "
        "FROM transactions t JOIN assets a ON t.asset_id=a.id "
        "WHERE t.user_id=? "
        "ORDER BY t.transaction_date ASC", params);
    if (!result) { respond_error(c, 500, "查询失败"); return; }

    double total_gain = 0, total_loss = 0;
    double total_cost_basis = 0;          // for display (includes fee, matches DB cost_basis)
    double total_cost_for_pnl = 0;         // for PnL avg_cost (excludes fee in numerator)
    double total_quantity = 0;
    double total_realized_pnl = 0;
    int total_trades = 0;
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
            total_cost_basis += amt + fee;   // database cost_basis includes fee
            total_cost_for_pnl += amt;        // PnL avg_cost excludes fee
            total_quantity += qty;
        } else if (strcmp(type, "sell") == 0 && qty > 0) {
            // avg_cost for PnL: uses cost_for_pnl (excludes fee)
            double avg_cost = total_quantity > 0 ? total_cost_for_pnl / total_quantity : 0;
            total_realized_pnl += amt - qty * avg_cost;
            // Reduce display cost_basis proportionally on sell (includes fee portion)
            double cost_reduction = total_quantity > 0
                ? (total_cost_basis / total_quantity) * qty : 0;
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
    csilk_json_t* pos_rows = csilk_db_query_param_json(pool,
        "SELECT COALESCE(SUM(quantity),0) as total_qty, "
        "COALESCE(SUM(cost_basis),0) as total_cost, "
        "COALESCE(SUM(current_value),0) as total_market "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('stock','fund','bond','crypto')", params);
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

void report_asset_summary(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, NULL };

    csilk_json_t* rows = csilk_db_query_param_json(pool,
        "SELECT c.name as name, c.asset_type, SUM(a.current_value) as value "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? GROUP BY c.name, c.asset_type", params);
    double current_assets = 0, current_liabs = 0;
    csilk_json_t* by_cat = csilk_json_array();
    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* row = csilk_json_array_get(rows, i);
            double v = db_get_num(row, "value");
            const char* atype = csilk_json_get_string(row, "asset_type");
            int is_liab = (strcmp(atype, "loan") == 0 || strcmp(atype, "credit_card") == 0 ||
                          strcmp(atype, "other_liability") == 0);
            if (is_liab) current_liabs += v; else current_assets += v;
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_string(item, "name", csilk_json_get_string(row, "name"));
            csilk_json_add_number(item, "value", v);
            csilk_json_add_bool(item, "is_liability", is_liab);
            csilk_json_array_append(by_cat, item);
        }
        csilk_json_free(rows);
    }
    // 30-day change estimate from transactions
    csilk_json_t* change_result = csilk_db_query_param_json(pool,
        "SELECT COALESCE(SUM(CASE WHEN direction='in' THEN amount ELSE -amount END),0) as net_change FROM transactions "
        "WHERE user_id=? AND transaction_date >= date('now','-30 days')", params);
    double change_30d = 0;
    if (change_result && csilk_json_array_size(change_result) > 0)
        change_30d = db_get_num(csilk_json_array_get(change_result, 0), "net_change");
    if (change_result) csilk_json_free(change_result);
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

// GET /api/summary — dashboard aggregate: net worth + category breakdown + 30-day trend
void summary_get(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params1[] = { uid_str, NULL };

    // Category breakdown (asset categories only; liabilities excluded from pie)
    csilk_json_t* rows = csilk_db_query_param_json(pool,
        "SELECT c.name as category_name, SUM(a.current_value) as value "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type NOT IN ('loan','credit_card','other_liability') "
        "GROUP BY c.name ORDER BY value DESC", params1);
    if (!rows) { respond_error(c, 500, "查询失败"); return; }
    double total_assets = 0, total_liabilities = 0;
    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        total_assets += db_get_num(row, "value");
    }
    csilk_json_t* breakdown = csilk_json_array();
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        double v = db_get_num(row, "value");
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "category_name", csilk_json_get_string(row, "category_name"));
        csilk_json_add_number(item, "value", v);
        csilk_json_add_number(item, "pct", total_assets > 0 ? (v / total_assets * 100) : 0);
        csilk_json_array_append(breakdown, item);
    }
    csilk_json_free(rows);

    // Total liabilities for net worth
    csilk_json_t* liab_rows = csilk_db_query_param_json(pool,
        "SELECT COALESCE(SUM(a.current_value),0) as total "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability')", params1);
    if (liab_rows && csilk_json_array_size(liab_rows) > 0) {
        total_liabilities = db_get_num(csilk_json_array_get(liab_rows, 0), "total");
        csilk_json_free(liab_rows);
    }

    // 30-day net worth trend (daily snapshots, asset counted from its updated_at)
    const char* params2[] = { uid_str, uid_str, NULL };
    csilk_json_t* trend_rows = csilk_db_query_param_json(pool,
        "SELECT json_group_array(json_object('date', d, 'net_worth', nw)) as trend FROM ("
        "WITH RECURSIVE dates(i) AS (SELECT 0 UNION ALL SELECT i+1 FROM dates WHERE i < 29) "
        "SELECT date('now','-'||(29-i)||' days') as d, "
        "(SELECT COALESCE(SUM(a.current_value),0) FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type NOT IN ('loan','credit_card','other_liability') "
        "AND a.updated_at < date('now','-'||(29-i)||' days','+1 day')) - "
        "(SELECT COALESCE(SUM(a.current_value),0) FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.user_id=? AND c.asset_type IN ('loan','credit_card','other_liability') "
        "AND a.updated_at < date('now','-'||(29-i)||' days','+1 day')) as nw "
        "FROM dates)", params2);

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
    if (trend_rows) csilk_json_free(trend_rows);
    respond_ok(c, resp);
}

/* ----------------------------------------------------------------
 * GET /api/reports/holdings
 * 持仓报表：按投资类资产聚合浮动盈亏 + 已实现盈亏
 * PnL 口径与 report_transaction_performance 完全一致（按资产分组）
 * ---------------------------------------------------------------- */
typedef struct {
    int64_t asset_id;
    double cost_for_pnl;
    double qty;
    double realized;
} holding_pnl_t;

static int64_t holding_find(holding_pnl_t* arr, size_t n, int64_t asset_id)
{
    for (size_t i = 0; i < n; i++) {
        if (arr[i].asset_id == asset_id) {
            return (int64_t)i;
        }
    }
    return -1;
}

void report_holdings(csilk_ctx_t* c)
{
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, NULL };

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

    size_t hn = csilk_json_array_size(hold_rows);
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
    const char* tx_sql =
        "SELECT asset_id, transaction_type, quantity, amount "
        "FROM transactions WHERE user_id = ? ORDER BY transaction_date ASC";
    csilk_json_t* tx_rows = csilk_db_query_param_json(pool, tx_sql, params);
    if (tx_rows) {
        size_t tn = csilk_json_array_size(tx_rows);
        for (size_t i = 0; i < tn; i++) {
            csilk_json_t* t = csilk_json_array_get(tx_rows, i);
            const char* type = csilk_json_get_string(t, "transaction_type");
            double amt = db_get_num(t, "amount");
            double qty = db_get_num(t, "quantity");
            int64_t aid = (int64_t)db_get_num(t, "asset_id");
            if (!type) {
                continue;
            }
            if (strcmp(type, "buy") != 0 && strcmp(type, "sell") != 0 &&
                strcmp(type, "income") != 0) {
                continue;               /* fee 等行跳过，与 performance 一致 */
            }
            int64_t idx = holding_find(accs, hn, aid);
            if (idx < 0) {
                continue;               /* 非投资类资产的交易，不计入持仓报表 */
            }
            if (strcmp(type, "buy") == 0) {
                accs[idx].cost_for_pnl += amt;  /* 不含 fee，与 performance 口径一致 */
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
    double total_market = 0.0, total_cost = 0.0, total_floating = 0.0, total_realized = 0.0;

    for (size_t i = 0; i < hn; i++) {
        csilk_json_t* row = csilk_json_array_get(hold_rows, i);
        double quantity = db_get_num(row, "quantity");
        double net_value = db_get_num(row, "net_value");
        double cost_basis = db_get_num(row, "cost_basis");
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

    double sum_pct = (total_cost == 0.0) ? 0.0 : (total_floating / total_cost) * 100.0;
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
