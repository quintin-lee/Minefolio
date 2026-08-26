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

void
report_expense_monthly(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");
    char        year_buf[8] = {0}, month_buf[4] = {0};
    if (!year_str || !month_str) {
        time_t     now = time(NULL);
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
    const char* params[] = {uid_str, date_pattern, NULL};

    csilk_db_pool_t* pool = db_get_pool();

    csilk_json_t* totals = csilk_db_query_param_json(
        pool,
        "SELECT COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as "
        "total_income, "
        "COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as total_expense "
        "FROM daily_expenses WHERE user_id=? AND expense_date LIKE ?",
        params);
    double income = 0, expense = 0;
    if (totals && csilk_json_array_size(totals) > 0) {
        income = db_get_num(csilk_json_array_get(totals, 0), "total_income");
        expense = db_get_num(csilk_json_array_get(totals, 0), "total_expense");
    }
    if (totals) {
        csilk_json_free(totals);
    }

    csilk_json_t* by_cat = csilk_db_query_param_json(
        pool,
        "SELECT c.name as name, de.expense_type, SUM(de.amount) as amount "
        "FROM daily_expenses de JOIN categories c ON de.category_id=c.id "
        "WHERE de.user_id=? AND de.expense_date LIKE ? "
        "GROUP BY c.name, de.expense_type ORDER BY amount DESC",
        params);

    csilk_json_t* by_tag = csilk_db_query_param_json(
        pool,
        "SELECT t.name as tag_name, SUM(de.amount) as amount, COUNT(*) as count "
        "FROM daily_expenses de JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id "
        "WHERE de.user_id=? AND de.expense_date LIKE ? "
        "GROUP BY t.name ORDER BY amount DESC",
        params);

    csilk_json_t* daily = csilk_db_query_param_json(
        pool,
        "SELECT expense_date, "
        "COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as income, "
        "COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as expense "
        "FROM daily_expenses WHERE user_id=? AND expense_date LIKE ? "
        "GROUP BY expense_date ORDER BY expense_date",
        params);

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

void
report_expense_trend(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    const char* months_str = csilk_get_query(c, "months");
    int         months = months_str ? atoi(months_str) : 6;
    if (months <= 0 || months > 24) {
        months = 6;
    }

    char uid_str[32], months_buf[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(months_buf, sizeof(months_buf), "%d", months);
    const char* params[] = {uid_str, months_buf, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    result = csilk_db_query_param_json(
        pool,
        "SELECT SUBSTR(expense_date,1,7) as period, "
        "COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as income, "
        "COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as expense "
        "FROM daily_expenses WHERE user_id=? AND expense_date >= date('now','-'||?||' months') "
        "GROUP BY SUBSTR(expense_date,1,7) ORDER BY period",
        params);

    if (!result) {
        respond_error(c, 500, "查询失败");
        return;
    }
    csilk_json_t* resp = csilk_json_object();
    csilk_json_t* labels = csilk_json_array();
    csilk_json_t* income_arr = csilk_json_array();
    csilk_json_t* expense_arr = csilk_json_array();
    size_t        n = csilk_json_array_size(result);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(result, i);
        csilk_json_array_append(labels,
                                csilk_json_string_new(csilk_json_get_string(row, "period")));
        csilk_json_array_append(income_arr, csilk_json_number(db_get_num(row, "income")));
        csilk_json_array_append(expense_arr, csilk_json_number(db_get_num(row, "expense")));
    }
    csilk_json_add_array(resp, "labels", labels);
    csilk_json_add_array(resp, "income", income_arr);
    csilk_json_add_array(resp, "expense", expense_arr);
    csilk_json_free(result);
    respond_ok(c, resp);
}

void
report_expense_yearly(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* year_str = csilk_get_query(c, "year");
    char        year_buf[8] = {0};
    if (year_str && strlen(year_str) > 0) {
        snprintf(year_buf, sizeof(year_buf), "%s", year_str);
    } else {
        time_t now = time(NULL);
        strftime(year_buf, sizeof(year_buf), "%Y", localtime(&now));
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = {uid_str, year_buf, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rows = csilk_db_query_param_json(
        pool,
        "SELECT CAST(SUBSTR(expense_date,6,2) AS INTEGER) as m, "
        "COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as income, "
        "COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as expense "
        "FROM daily_expenses WHERE user_id=? AND SUBSTR(expense_date,1,4)=? "
        "GROUP BY m ORDER BY m",
        params);
    if (!rows) {
        respond_error(c, 500, "查询失败");
        return;
    }

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

void
report_expense_category(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");
    char        year_buf[8] = {0};
    time_t      now = time(NULL);
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
    const char* params[] = {uid_str, period_pattern, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rows = csilk_db_query_param_json(
        pool,
        "SELECT c.name as name, SUM(de.amount) as amount "
        "FROM daily_expenses de JOIN categories c ON de.category_id=c.id "
        "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date LIKE ? "
        "GROUP BY c.name ORDER BY amount DESC",
        params);
    if (!rows) {
        respond_error(c, 500, "查询失败");
        return;
    }
    double total = 0;
    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        total += db_get_num(csilk_json_array_get(rows, i), "amount");
    }
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", period);
    csilk_json_t* items = csilk_json_array();
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        double        amt = db_get_num(row, "amount");
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

void
report_expense_tag(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");
    char        year_buf[8] = {0};
    time_t      now = time(NULL);
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
    const char* params[] = {uid_str, period_pattern, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rows = csilk_db_query_param_json(
        pool,
        "SELECT t.name as tag_name, SUM(de.amount) as amount, COUNT(*) as count "
        "FROM daily_expenses de JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id "
        "WHERE de.user_id=? AND de.expense_type='expense' AND de.expense_date LIKE ? "
        "GROUP BY t.name ORDER BY amount DESC",
        params);
    if (!rows) {
        respond_error(c, 500, "查询失败");
        return;
    }
    double total = 0;
    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        total += db_get_num(csilk_json_array_get(rows, i), "amount");
    }
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", period);
    csilk_json_t* items = csilk_json_array();
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        double        amt = db_get_num(row, "amount");
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
