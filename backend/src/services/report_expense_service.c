#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include "services/market/exchange_rate_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

typedef struct {
    int64_t asset_id;
    double  cost_for_pnl;
    double  qty;
    double  realized;
} holding_pnl_t;

void
report_expense_monthly(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
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

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    const char* params[] = {uid_str, lid_str, date_pattern, NULL};

    csilk_db_pool_t* pool = db_get_pool();

    // Query expenses for the month to calculate converted totals, category breakdown, and daily breakdown
    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT de.expense_date, de.expense_type, de.amount, de.currency, c.name as category_name "
        "FROM daily_expenses de JOIN categories c ON de.category_id=c.id "
        "AND c.user_id=de.user_id AND c.ledger_id=de.ledger_id "
        "WHERE de.user_id=? AND de.ledger_id=? AND CAST(de.expense_date AS TEXT) LIKE ? "
        "ORDER BY de.expense_date ASC",
        params);

    double income = 0, expense = 0;

    typedef struct {
        char   name[128];
        char   expense_type[32];
        double amount;
    } cat_item_t;

    cat_item_t cats[128];
    int        cat_count = 0;

    typedef struct {
        char   date[32];
        double income;
        double expense;
    } daily_item_t;

    daily_item_t daily_items[64];
    int          daily_count = 0;

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(rows, i);
            const char*   type = csilk_json_get_string(r, "expense_type");
            const char*   cur = csilk_json_get_string(r, "currency");
            const char*   cname = csilk_json_get_string(r, "category_name");
            const char*   dt = csilk_json_get_string(r, "expense_date");
            double        amt = db_get_num(r, "amount") * exchange_rate_get_to_cny(cur);

            if (type && strcmp(type, "income") == 0) {
                income += amt;
            } else {
                expense += amt;
            }

            // Category aggregation
            if (cname && type) {
                int found = -1;
                for (int k = 0; k < cat_count; k++) {
                    if (strcmp(cats[k].name, cname) == 0 &&
                        strcmp(cats[k].expense_type, type) == 0) {
                        found = k;
                        break;
                    }
                }
                if (found >= 0) {
                    cats[found].amount += amt;
                } else if (cat_count < 128) {
                    strncpy(cats[cat_count].name, cname, sizeof(cats[0].name) - 1);
                    cats[cat_count].name[sizeof(cats[0].name) - 1] = '\0';
                    strncpy(cats[cat_count].expense_type, type, sizeof(cats[0].expense_type) - 1);
                    cats[cat_count].expense_type[sizeof(cats[0].expense_type) - 1] = '\0';
                    cats[cat_count].amount = amt;
                    cat_count++;
                }
            }

            // Daily aggregation
            if (dt) {
                int d_idx = -1;
                for (int d = 0; d < daily_count; d++) {
                    if (strcmp(daily_items[d].date, dt) == 0) {
                        d_idx = d;
                        break;
                    }
                }
                if (d_idx < 0 && daily_count < 64) {
                    d_idx = daily_count++;
                    strncpy(daily_items[d_idx].date, dt, sizeof(daily_items[0].date) - 1);
                    daily_items[d_idx].date[sizeof(daily_items[0].date) - 1] = '\0';
                    daily_items[d_idx].income = 0;
                    daily_items[d_idx].expense = 0;
                }
                if (d_idx >= 0) {
                    if (type && strcmp(type, "income") == 0) {
                        daily_items[d_idx].income += amt;
                    } else {
                        daily_items[d_idx].expense += amt;
                    }
                }
            }
        }
        csilk_json_free(rows);
    }

    // Sort categories by amount DESC
    for (int i = 0; i < cat_count - 1; i++) {
        for (int j = i + 1; j < cat_count; j++) {
            if (cats[j].amount > cats[i].amount) {
                cat_item_t tmp = cats[i];
                cats[i] = cats[j];
                cats[j] = tmp;
            }
        }
    }

    csilk_json_t* by_cat = csilk_json_array();
    for (int k = 0; k < cat_count; k++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", cats[k].name);
        csilk_json_add_string(item, "expense_type", cats[k].expense_type);
        csilk_json_add_number(item, "amount", cats[k].amount);
        csilk_json_array_append(by_cat, item);
    }

    // Tag aggregation with currency conversion
    csilk_json_t* tag_rows = csilk_db_query_param_json(
        pool,
        "SELECT t.name as tag_name, de.amount, de.currency "
        "FROM daily_expenses de JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id AND t.user_id=de.user_id "
        "WHERE de.user_id=? AND de.ledger_id=? AND CAST(de.expense_date AS TEXT) LIKE ?",
        params);

    typedef struct {
        char    name[128];
        double  amount;
        int64_t count;
    } tag_item_t;

    tag_item_t tag_items[128];
    int        tag_count = 0;

    if (tag_rows) {
        size_t n = csilk_json_array_size(tag_rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(tag_rows, i);
            const char*   tname = csilk_json_get_string(r, "tag_name");
            const char*   cur = csilk_json_get_string(r, "currency");
            double        amt = db_get_num(r, "amount") * exchange_rate_get_to_cny(cur);
            if (!tname) {
                continue;
            }

            int found = -1;
            for (int k = 0; k < tag_count; k++) {
                if (strcmp(tag_items[k].name, tname) == 0) {
                    found = k;
                    break;
                }
            }
            if (found >= 0) {
                tag_items[found].amount += amt;
                tag_items[found].count++;
            } else if (tag_count < 128) {
                strncpy(tag_items[tag_count].name, tname, sizeof(tag_items[0].name) - 1);
                tag_items[tag_count].name[sizeof(tag_items[0].name) - 1] = '\0';
                tag_items[tag_count].amount = amt;
                tag_items[tag_count].count = 1;
                tag_count++;
            }
        }
        csilk_json_free(tag_rows);
    }

    // Sort tags by amount DESC
    for (int i = 0; i < tag_count - 1; i++) {
        for (int j = i + 1; j < tag_count; j++) {
            if (tag_items[j].amount > tag_items[i].amount) {
                tag_item_t tmp = tag_items[i];
                tag_items[i] = tag_items[j];
                tag_items[j] = tmp;
            }
        }
    }

    csilk_json_t* by_tag = csilk_json_array();
    for (int k = 0; k < tag_count; k++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "tag_name", tag_items[k].name);
        csilk_json_add_number(item, "amount", tag_items[k].amount);
        csilk_json_add_number(item, "count", (double)tag_items[k].count);
        csilk_json_array_append(by_tag, item);
    }

    csilk_json_t* daily = csilk_json_array();
    for (int d = 0; d < daily_count; d++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "expense_date", daily_items[d].date);
        csilk_json_add_number(item, "income", daily_items[d].income);
        csilk_json_add_number(item, "expense", daily_items[d].expense);
        csilk_json_array_append(daily, item);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "year", atoll(year_str));
    csilk_json_add_number(resp, "month", atoll(month_str));
    csilk_json_add_number(resp, "total_income", income);
    csilk_json_add_number(resp, "total_expense", expense);
    csilk_json_add_number(resp, "balance", income - expense);
    csilk_json_add_array(resp, "by_category", by_cat);
    csilk_json_add_array(resp, "by_tag", by_tag);
    csilk_json_add_array(resp, "daily_breakdown", daily);
    respond_ok(c, resp);
}

void
report_expense_trend(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }
    const char* months_str = csilk_get_query(c, "months");
    int         months = months_str ? atoi(months_str) : 6;
    if (months <= 0 || months > 24) {
        months = 6;
    }

    char uid_str[32], lid_str[32], cutoff_buf[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    /* Portable replacement for date('now','-N months'): compute the cutoff in C so
     * both SQLite (TEXT compare) and PG (DATE compare) accept it as a plain param. */
    {
        time_t    now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        tmv.tm_mon -= months;
        mktime(&tmv);
        strftime(cutoff_buf, sizeof(cutoff_buf), "%Y-%m-%d", &tmv);
    }
    const char* params[] = {uid_str, lid_str, cutoff_buf, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    result = csilk_db_query_param_json(
        pool,
        "SELECT SUBSTR(CAST(expense_date AS TEXT),1,7) as period, "
        "expense_type, amount, currency "
        "FROM daily_expenses WHERE user_id=? AND ledger_id=? AND expense_date >= ? "
        "ORDER BY period ASC",
        params);

    if (!result) {
        respond_error(c, 500, "查询失败");
        return;
    }

    typedef struct {
        char   period[16];
        double income;
        double expense;
    } trend_period_t;

    trend_period_t p_items[64];
    int            p_count = 0;

    size_t n = csilk_json_array_size(result);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(result, i);
        const char*   period = csilk_json_get_string(row, "period");
        const char*   type = csilk_json_get_string(row, "expense_type");
        const char*   cur = csilk_json_get_string(row, "currency");
        double        amt = db_get_num(row, "amount") * exchange_rate_get_to_cny(cur);
        if (!period) {
            continue;
        }

        int found = -1;
        for (int k = 0; k < p_count; k++) {
            if (strcmp(p_items[k].period, period) == 0) {
                found = k;
                break;
            }
        }
        if (found < 0 && p_count < 64) {
            found = p_count++;
            strncpy(p_items[found].period, period, sizeof(p_items[found].period) - 1);
            p_items[found].period[sizeof(p_items[found].period) - 1] = '\0';
            p_items[found].income = 0;
            p_items[found].expense = 0;
        }
        if (found >= 0) {
            if (type && strcmp(type, "income") == 0) {
                p_items[found].income += amt;
            } else {
                p_items[found].expense += amt;
            }
        }
    }
    csilk_json_free(result);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_t* labels = csilk_json_array();
    csilk_json_t* income_arr = csilk_json_array();
    csilk_json_t* expense_arr = csilk_json_array();
    for (int k = 0; k < p_count; k++) {
        csilk_json_array_append(labels, csilk_json_string_new(p_items[k].period));
        csilk_json_array_append(income_arr, csilk_json_number(p_items[k].income));
        csilk_json_array_append(expense_arr, csilk_json_number(p_items[k].expense));
    }
    csilk_json_add_array(resp, "labels", labels);
    csilk_json_add_array(resp, "income", income_arr);
    csilk_json_add_array(resp, "expense", expense_arr);
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

    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    const char* params[] = {uid_str, lid_str, year_buf, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rows = csilk_db_query_param_json(
        pool,
        "SELECT CAST(SUBSTR(CAST(expense_date AS TEXT),6,2) AS INTEGER) as m, "
        "expense_type, amount, currency "
        "FROM daily_expenses WHERE user_id=? AND ledger_id=? AND SUBSTR(CAST(expense_date AS "
        "TEXT),1,4)=? "
        "ORDER BY m",
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
        csilk_json_t* row = csilk_json_array_get(rows, i);
        int           m = (int)db_get_num(row, "m");
        const char*   type = csilk_json_get_string(row, "expense_type");
        const char*   cur = csilk_json_get_string(row, "currency");
        double        amt = db_get_num(row, "amount") * exchange_rate_get_to_cny(cur);
        if (m >= 1 && m <= 12) {
            if (type && strcmp(type, "income") == 0) {
                income_by_month[m] += amt;
            } else {
                expense_by_month[m] += amt;
            }
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
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
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

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    const char* params[] = {uid_str, lid_str, period_pattern, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rows = csilk_db_query_param_json(
        pool,
        "SELECT c.name as name, de.amount, de.currency "
        "FROM daily_expenses de JOIN categories c ON de.category_id=c.id "
        "AND c.user_id=de.user_id AND c.ledger_id=de.ledger_id "
        "WHERE de.user_id=? AND de.ledger_id=? AND "
        "de.expense_type='expense' AND CAST(de.expense_date AS TEXT) LIKE ?",
        params);
    if (!rows) {
        respond_error(c, 500, "查询失败");
        return;
    }

    typedef struct {
        char   name[128];
        double amount;
    } cat_agg_t;

    cat_agg_t cats[128];
    int       cat_count = 0;
    double    total = 0;

    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        const char*   name = csilk_json_get_string(row, "name");
        const char*   cur = csilk_json_get_string(row, "currency");
        double        amt = db_get_num(row, "amount") * exchange_rate_get_to_cny(cur);
        total += amt;
        if (!name) {
            continue;
        }

        int found = -1;
        for (int k = 0; k < cat_count; k++) {
            if (strcmp(cats[k].name, name) == 0) {
                found = k;
                break;
            }
        }
        if (found >= 0) {
            cats[found].amount += amt;
        } else if (cat_count < 128) {
            strncpy(cats[cat_count].name, name, sizeof(cats[0].name) - 1);
            cats[cat_count].name[sizeof(cats[0].name) - 1] = '\0';
            cats[cat_count].amount = amt;
            cat_count++;
        }
    }
    csilk_json_free(rows);

    // Sort categories by amount DESC
    for (int i = 0; i < cat_count - 1; i++) {
        for (int j = i + 1; j < cat_count; j++) {
            if (cats[j].amount > cats[i].amount) {
                cat_agg_t tmp = cats[i];
                cats[i] = cats[j];
                cats[j] = tmp;
            }
        }
    }

    // Calculate rounded percentages (2 decimals) and balance largest remainder
    double pcts[128] = {0};
    double pct_sum = 0.0;
    int    max_idx = 0;
    double max_amt = -1.0;

    for (int k = 0; k < cat_count; k++) {
        if (total > 0.0) {
            pcts[k] = round((cats[k].amount / total * 100.0) * 100.0) / 100.0;
        } else {
            pcts[k] = 0.0;
        }
        pct_sum += pcts[k];
        if (cats[k].amount > max_amt) {
            max_amt = cats[k].amount;
            max_idx = k;
        }
    }

    if (total > 0.0 && cat_count > 0) {
        double delta = round((100.0 - pct_sum) * 100.0) / 100.0;
        if (fabs(delta) <= 0.05) {
            pcts[max_idx] += delta;
        }
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", period);
    csilk_json_t* items = csilk_json_array();
    for (int k = 0; k < cat_count; k++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", cats[k].name);
        csilk_json_add_number(item, "amount", cats[k].amount);
        csilk_json_add_number(item, "pct", pcts[k]);
        csilk_json_array_append(items, item);
    }
    csilk_json_add_array(resp, "items", items);
    respond_ok(c, resp);
}

void
report_expense_tag(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
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

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);
    const char* params[] = {uid_str, lid_str, period_pattern, NULL};

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rows = csilk_db_query_param_json(
        pool,
        "SELECT t.name as tag_name, de.amount, de.currency "
        "FROM daily_expenses de JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id AND t.user_id=de.user_id "
        "WHERE de.user_id=? AND de.ledger_id=? AND de.expense_type='expense' AND "
        "CAST(de.expense_date AS TEXT) "
        "LIKE ? ",
        params);
    if (!rows) {
        respond_error(c, 500, "查询失败");
        return;
    }

    typedef struct {
        char    name[128];
        double  amount;
        int64_t count;
    } tag_agg_t;

    tag_agg_t tags[128];
    int       tag_count = 0;
    double    total = 0;

    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        const char*   name = csilk_json_get_string(row, "tag_name");
        const char*   cur = csilk_json_get_string(row, "currency");
        double        amt = db_get_num(row, "amount") * exchange_rate_get_to_cny(cur);
        total += amt;
        if (!name) {
            continue;
        }

        int found = -1;
        for (int k = 0; k < tag_count; k++) {
            if (strcmp(tags[k].name, name) == 0) {
                found = k;
                break;
            }
        }
        if (found >= 0) {
            tags[found].amount += amt;
            tags[found].count++;
        } else if (tag_count < 128) {
            strncpy(tags[tag_count].name, name, sizeof(tags[0].name) - 1);
            tags[tag_count].name[sizeof(tags[0].name) - 1] = '\0';
            tags[tag_count].amount = amt;
            tags[tag_count].count = 1;
            tag_count++;
        }
    }
    csilk_json_free(rows);

    // Sort tags by amount DESC
    for (int i = 0; i < tag_count - 1; i++) {
        for (int j = i + 1; j < tag_count; j++) {
            if (tags[j].amount > tags[i].amount) {
                tag_agg_t tmp = tags[i];
                tags[i] = tags[j];
                tags[j] = tmp;
            }
        }
    }

    // Calculate rounded percentages (2 decimals) and balance largest remainder
    double pcts[128] = {0};
    double pct_sum = 0.0;
    int    max_idx = 0;
    double max_amt = -1.0;

    for (int k = 0; k < tag_count; k++) {
        if (total > 0.0) {
            pcts[k] = round((tags[k].amount / total * 100.0) * 100.0) / 100.0;
        } else {
            pcts[k] = 0.0;
        }
        pct_sum += pcts[k];
        if (tags[k].amount > max_amt) {
            max_amt = tags[k].amount;
            max_idx = k;
        }
    }

    if (total > 0.0 && tag_count > 0) {
        double delta = round((100.0 - pct_sum) * 100.0) / 100.0;
        if (fabs(delta) <= 0.05) {
            pcts[max_idx] += delta;
        }
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", period);
    csilk_json_t* items = csilk_json_array();
    for (int k = 0; k < tag_count; k++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "tag_name", tags[k].name);
        csilk_json_add_number(item, "amount", tags[k].amount);
        csilk_json_add_number(item, "count", (double)tags[k].count);
        csilk_json_add_number(item, "pct", pcts[k]);
        csilk_json_array_append(items, item);
    }
    csilk_json_add_array(resp, "items", items);
    respond_ok(c, resp);
}
