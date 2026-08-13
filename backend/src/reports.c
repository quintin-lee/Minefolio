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
        "t.quantity, t.price_per_unit, t.amount "
        "FROM transactions t JOIN assets a ON t.asset_id=a.id "
        "WHERE t.user_id=? AND t.transaction_type NOT IN ('transfer_in', 'transfer_out') "
        "ORDER BY t.transaction_date DESC", params);
    if (!result) { respond_error(c, 500, "查询失败"); return; }

    double total_gain = 0, total_loss = 0;
    int total_trades = 0;
    csilk_json_t* trades = csilk_json_array();
    size_t n = csilk_json_array_size(result);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(result, i);
        const char* type = csilk_json_get_string(row, "transaction_type");
        double amt = db_get_num(row, "amount");
        const char* dir = csilk_json_get_string(row, "direction");
        if (dir && strcmp(dir, "in") == 0) {
            total_gain += amt;
        } else {
            total_loss += amt;
        }
        total_trades++;

        csilk_json_t* trade = csilk_json_object();
        csilk_json_add_number(trade, "id", db_get_num(row, "id"));
        csilk_json_add_string(trade, "asset_name", csilk_json_get_string(row, "asset_name"));
        csilk_json_add_string(trade, "type", type ? type : "");
        csilk_json_add_string(trade, "date", csilk_json_get_string(row, "transaction_date"));
        csilk_json_add_number(trade, "quantity", db_get_num(row, "quantity"));
        csilk_json_add_number(trade, "price", db_get_num(row, "price_per_unit"));
        csilk_json_add_number(trade, "amount", amt);
        csilk_json_array_append(trades, trade);
    }
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "total_trades", total_trades);
    csilk_json_add_number(resp, "total_gain", total_gain);
    csilk_json_add_number(resp, "total_loss", total_loss);
    csilk_json_add_number(resp, "net_gain", total_gain - total_loss);
    csilk_json_add_array(resp, "trades", trades);
    csilk_json_free(result);
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
