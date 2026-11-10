/**
 * @file report_expense_service.c
 * @brief 收支报表查询实现（Application Layer - 报表域）
 *
 * 依赖：pool（db_get_pool()）+ report_query_t 纯参数，不依赖 csilk_ctx_t / ctx_* / respond_*。
 * 结果通过 report_result_t.data_payload 承载 csilk_json_t*（由调用方释放/挂载）。
 */

#include "services/report_expense_service.h"
#include "application/report/commands.h"
#include "common/db.h"
#include "common/response.h"
#include "core/financial/money.h"
#include "core/financial/currency.h"
#include "services/market/exchange_rate_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

void
report_expense_monthly(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    const char* year_str = q->year;
    const char* month_str = q->month;
    char        year_buf[8] = {0}, month_buf[4] = {0};
    if (!year_str || !month_str) {
        time_t     now = time(NULL);
        struct tm  tm_now_storage;
        struct tm* tm_now = localtime_r(&now, &tm_now_storage);
        strftime(year_buf, sizeof(year_buf), "%Y", tm_now);
        strftime(month_buf, sizeof(month_buf), "%m", tm_now);
        year_str = year_buf;
        month_str = month_buf;
    }
    char date_pattern[32];
    snprintf(date_pattern, sizeof(date_pattern), "%s-%02d-%%", year_str, atoi(month_str));

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);
    const char* params[] = {uid_str, lid_str, date_pattern, NULL};

    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT de.expense_date, de.expense_type, de.amount, de.currency, c.name as category_name "
        "FROM daily_expenses de JOIN categories c ON de.category_id=c.id "
        "AND c.user_id=de.user_id AND c.ledger_id=de.ledger_id "
        "WHERE de.user_id=? AND de.ledger_id=? AND CAST(de.expense_date AS TEXT) LIKE ? "
        "ORDER BY de.expense_date ASC",
        params);

    currency_t base_cur = currency_from_str("CNY");
    money_t    income_m = money_zero(base_cur);
    money_t    expense_m = money_zero(base_cur);

    typedef struct {
        char    name[128];
        char    expense_type[32];
        money_t amount;
    } cat_item_t;

    cat_item_t cats[128];
    int        cat_count = 0;

    typedef struct {
        char    date[32];
        money_t income;
        money_t expense;
    } daily_item_t;

    daily_item_t daily_items[64];
    int          daily_count = 0;

    if (rows) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(rows, i);
            const char*   type = csilk_json_get_string(r, "expense_type");
            const char*   cur_s = csilk_json_get_string(r, "currency");
            currency_t    cur = currency_from_str(cur_s ? cur_s : "CNY");
            const char*   cname = csilk_json_get_string(r, "category_name");
            const char*   dt = csilk_json_get_string(r, "expense_date");
            money_t       amt = exchange_rate_convert_m(db_get_money(r, "amount", cur), base_cur);
            money_t       acc;

            if (type && strcmp(type, "income") == 0) {
                if (money_add(income_m, amt, &acc) == DECIMAL_OK) {
                    income_m = acc;
                }
            } else {
                if (money_add(expense_m, amt, &acc) == DECIMAL_OK) {
                    expense_m = acc;
                }
            }

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
                    if (money_add(cats[found].amount, amt, &acc) == DECIMAL_OK) {
                        cats[found].amount = acc;
                    }
                } else if (cat_count < 128) {
                    strncpy(cats[cat_count].name, cname, sizeof(cats[0].name) - 1);
                    cats[cat_count].name[sizeof(cats[0].name) - 1] = '\0';
                    strncpy(cats[cat_count].expense_type, type, sizeof(cats[0].expense_type) - 1);
                    cats[cat_count].expense_type[sizeof(cats[0].expense_type) - 1] = '\0';
                    cats[cat_count].amount = amt;
                    cat_count++;
                }
            }

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
                    daily_items[d_idx].income = money_zero(base_cur);
                    daily_items[d_idx].expense = money_zero(base_cur);
                }
                if (d_idx >= 0) {
                    if (type && strcmp(type, "income") == 0) {
                        if (money_add(daily_items[d_idx].income, amt, &acc) == DECIMAL_OK) {
                            daily_items[d_idx].income = acc;
                        }
                    } else {
                        if (money_add(daily_items[d_idx].expense, amt, &acc) == DECIMAL_OK) {
                            daily_items[d_idx].expense = acc;
                        }
                    }
                }
            }
        }
        csilk_json_free(rows);
    }

    for (int i = 0; i < cat_count - 1; i++) {
        for (int j = i + 1; j < cat_count; j++) {
            if (money_cmp(cats[j].amount, cats[i].amount) > 0) {
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
        csilk_json_add_number(item, "amount", money_to_double(cats[k].amount));
        csilk_json_array_append(by_cat, item);
    }

    csilk_json_t* tag_rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT t.name as tag_name, de.amount, de.currency "
        "FROM daily_expenses de JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id AND t.user_id=de.user_id "
        "WHERE de.user_id=? AND de.ledger_id=? AND CAST(de.expense_date AS TEXT) LIKE ?",
        params);

    typedef struct {
        char    name[128];
        money_t amount;
        int64_t count;
    } tag_item_t;

    tag_item_t tag_items[128];
    int        tag_count = 0;

    if (tag_rows) {
        size_t n = csilk_json_array_size(tag_rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* r = csilk_json_array_get(tag_rows, i);
            const char*   tname = csilk_json_get_string(r, "tag_name");
            const char*   cur_s = csilk_json_get_string(r, "currency");
            currency_t    cur = currency_from_str(cur_s ? cur_s : "CNY");
            money_t       amt = exchange_rate_convert_m(db_get_money(r, "amount", cur), base_cur);
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
            money_t acc;
            if (found >= 0) {
                if (money_add(tag_items[found].amount, amt, &acc) == DECIMAL_OK) {
                    tag_items[found].amount = acc;
                }
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

    for (int i = 0; i < tag_count - 1; i++) {
        for (int j = i + 1; j < tag_count; j++) {
            if (money_cmp(tag_items[j].amount, tag_items[i].amount) > 0) {
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
        csilk_json_add_number(item, "amount", money_to_double(tag_items[k].amount));
        csilk_json_add_number(item, "count", (double)tag_items[k].count);
        csilk_json_array_append(by_tag, item);
    }

    csilk_json_t* daily = csilk_json_array();
    for (int d = 0; d < daily_count; d++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "expense_date", daily_items[d].date);
        csilk_json_add_number(item, "income", money_to_double(daily_items[d].income));
        csilk_json_add_number(item, "expense", money_to_double(daily_items[d].expense));
        csilk_json_array_append(daily, item);
    }

    money_t balance_m;
    if (money_sub(income_m, expense_m, &balance_m) != DECIMAL_OK) {
        balance_m = income_m;
    }
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "year", (double)parse_query_int_strict(year_str, 0));
    csilk_json_add_number(resp, "month", (double)parse_query_int_strict(month_str, 0));
    csilk_json_add_number(resp, "total_income", money_to_double(income_m));
    csilk_json_add_number(resp, "total_expense", money_to_double(expense_m));
    csilk_json_add_number(resp, "balance", money_to_double(balance_m));
    csilk_json_add_array(resp, "by_category", by_cat);
    csilk_json_add_array(resp, "by_tag", by_tag);
    csilk_json_add_array(resp, "daily_breakdown", daily);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}

void
report_expense_trend(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    int months = (q->days > 0) ? q->days : 6;
    if (months <= 0 || months > 24) {
        months = 6;
    }

    char uid_str[32], lid_str[32], cutoff_buf[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);
    {
        time_t    now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        tmv.tm_mon -= months;
        mktime(&tmv);
        strftime(cutoff_buf, sizeof(cutoff_buf), "%Y-%m-%d", &tmv);
    }
    const char* params[] = {uid_str, lid_str, cutoff_buf, NULL};

    csilk_json_t* result = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT SUBSTR(CAST(expense_date AS TEXT),1,7) as period, "
        "expense_type, amount, currency "
        "FROM daily_expenses WHERE user_id=? AND ledger_id=? AND expense_date >= ? "
        "ORDER BY period ASC",
        params);

    if (!result) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询失败");
        return;
    }

    currency_t base_cur = currency_from_str("CNY");
    typedef struct {
        char    period[16];
        money_t income;
        money_t expense;
    } trend_period_t;

    trend_period_t p_items[64];
    int            p_count = 0;

    size_t n = csilk_json_array_size(result);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(result, i);
        const char*   period = csilk_json_get_string(row, "period");
        const char*   type = csilk_json_get_string(row, "expense_type");
        const char*   cur_s = csilk_json_get_string(row, "currency");
        currency_t    cur = currency_from_str(cur_s ? cur_s : "CNY");
        money_t       amt = exchange_rate_convert_m(db_get_money(row, "amount", cur), base_cur);
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
        money_t acc;
        if (found < 0 && p_count < 64) {
            found = p_count++;
            strncpy(p_items[found].period, period, sizeof(p_items[found].period) - 1);
            p_items[found].period[sizeof(p_items[found].period) - 1] = '\0';
            p_items[found].income = money_zero(base_cur);
            p_items[found].expense = money_zero(base_cur);
        }
        if (found >= 0) {
            if (type && strcmp(type, "income") == 0) {
                if (money_add(p_items[found].income, amt, &acc) == DECIMAL_OK) {
                    p_items[found].income = acc;
                }
            } else {
                if (money_add(p_items[found].expense, amt, &acc) == DECIMAL_OK) {
                    p_items[found].expense = acc;
                }
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
        csilk_json_array_append(income_arr, csilk_json_number(money_to_double(p_items[k].income)));
        csilk_json_array_append(expense_arr,
                                csilk_json_number(money_to_double(p_items[k].expense)));
    }
    csilk_json_add_array(resp, "labels", labels);
    csilk_json_add_array(resp, "income", income_arr);
    csilk_json_add_array(resp, "expense", expense_arr);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}

void
report_expense_yearly(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    const char* year_str = q->year;
    char        year_buf[8] = {0};
    if (year_str && strlen(year_str) > 0) {
        snprintf(year_buf, sizeof(year_buf), "%s", year_str);
    } else {
        time_t    now = time(NULL);
        struct tm tm_local;
        strftime(year_buf, sizeof(year_buf), "%Y", localtime_r(&now, &tm_local));
    }

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);
    const char* params[] = {uid_str, lid_str, year_buf, NULL};

    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT CAST(SUBSTR(CAST(expense_date AS TEXT),6,2) AS INTEGER) as m, "
        "expense_type, amount, currency "
        "FROM daily_expenses WHERE user_id=? AND ledger_id=? AND SUBSTR(CAST(expense_date AS "
        "TEXT),1,4)=? "
        "ORDER BY m",
        params);
    if (!rows) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询失败");
        return;
    }

    currency_t base_cur = currency_from_str("CNY");
    money_t    income_by_month[13] = {0};
    money_t    expense_by_month[13] = {0};
    for (int m = 0; m < 13; m++) {
        income_by_month[m] = money_zero(base_cur);
        expense_by_month[m] = money_zero(base_cur);
    }
    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        int           m = (int)db_get_num(row, "m");
        const char*   type = csilk_json_get_string(row, "expense_type");
        const char*   cur_s = csilk_json_get_string(row, "currency");
        currency_t    cur = currency_from_str(cur_s ? cur_s : "CNY");
        money_t       amt = exchange_rate_convert_m(db_get_money(row, "amount", cur), base_cur);
        if (m >= 1 && m <= 12) {
            money_t acc;
            if (type && strcmp(type, "income") == 0) {
                if (money_add(income_by_month[m], amt, &acc) == DECIMAL_OK) {
                    income_by_month[m] = acc;
                }
            } else {
                if (money_add(expense_by_month[m], amt, &acc) == DECIMAL_OK) {
                    expense_by_month[m] = acc;
                }
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
        csilk_json_array_append(income_arr, csilk_json_number(money_to_double(income_by_month[m])));
        csilk_json_array_append(expense_arr,
                                csilk_json_number(money_to_double(expense_by_month[m])));
    }
    csilk_json_add_number(resp, "year", (double)parse_query_int_strict(year_buf, 0));
    csilk_json_add_array(resp, "labels", labels);
    csilk_json_add_array(resp, "income", income_arr);
    csilk_json_add_array(resp, "expense", expense_arr);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}

void
report_expense_category(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    const char* year_str = q->year;
    const char* month_str = q->month;
    char        year_buf[8] = {0};
    time_t      now = time(NULL);
    struct tm   tm_local;
    strftime(year_buf, sizeof(year_buf), "%Y", localtime_r(&now, &tm_local));
    char period[16], period_pattern[32];
    if (year_str && month_str) {
        snprintf(period, sizeof(period), "%s-%02d-", year_str, atoi(month_str));
    } else {
        snprintf(period, sizeof(period), "%s-", year_buf);
    }
    snprintf(period_pattern, sizeof(period_pattern), "%s%%", period);

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);
    const char* params[] = {uid_str, lid_str, period_pattern, NULL};

    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT c.name as name, de.amount, de.currency "
        "FROM daily_expenses de JOIN categories c ON de.category_id=c.id "
        "AND c.user_id=de.user_id AND c.ledger_id=de.ledger_id "
        "WHERE de.user_id=? AND de.ledger_id=? AND "
        "de.expense_type='expense' AND CAST(de.expense_date AS TEXT) LIKE ?",
        params);
    if (!rows) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询失败");
        return;
    }

    currency_t base_cur = currency_from_str("CNY");
    typedef struct {
        char    name[128];
        money_t amount;
    } cat_agg_t;

    cat_agg_t cats[128];
    int       cat_count = 0;
    money_t   total = money_zero(base_cur);

    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        const char*   name = csilk_json_get_string(row, "name");
        const char*   cur_s = csilk_json_get_string(row, "currency");
        currency_t    cur = currency_from_str(cur_s ? cur_s : "CNY");
        money_t       amt = exchange_rate_convert_m(db_get_money(row, "amount", cur), base_cur);
        money_t       acc;
        if (money_add(total, amt, &acc) == DECIMAL_OK) {
            total = acc;
        }
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
            if (money_add(cats[found].amount, amt, &acc) == DECIMAL_OK) {
                cats[found].amount = acc;
            }
        } else if (cat_count < 128) {
            strncpy(cats[cat_count].name, name, sizeof(cats[0].name) - 1);
            cats[cat_count].name[sizeof(cats[0].name) - 1] = '\0';
            cats[cat_count].amount = amt;
            cat_count++;
        }
    }
    csilk_json_free(rows);

    for (int i = 0; i < cat_count - 1; i++) {
        for (int j = i + 1; j < cat_count; j++) {
            if (money_cmp(cats[j].amount, cats[i].amount) > 0) {
                cat_agg_t tmp = cats[i];
                cats[i] = cats[j];
                cats[j] = tmp;
            }
        }
    }

    percentage_t pcts[128];
    percentage_t pct_sum = percentage_zero();
    int          max_idx = 0;
    money_t      max_amt = money_zero(base_cur);
    for (int k = 0; k < cat_count; k++) {
        percentage_t pct;
        if (decimal_is_zero(total.amount)) {
            pct = percentage_zero();
        } else if (percentage_calc(cats[k].amount, total, 2, ROUND_HALF_UP, &pct) != DECIMAL_OK) {
            pct = percentage_zero();
        }
        pcts[k] = pct;
        decimal_add(pct_sum.percent, pct.percent, &pct_sum.percent);
        if (money_cmp(cats[k].amount, max_amt) > 0) {
            max_amt = cats[k].amount;
            max_idx = k;
        }
    }

    /* 尾差补偿：若百分比合计与 100% 的偏差在 ±0.05 以内，补到最大项使合计闭合 */
    if (!decimal_is_zero(total.amount) && cat_count > 0) {
        decimal_t hundred = decimal_from_int(100);
        decimal_t diff;
        decimal_sub(hundred, pct_sum.percent, &diff);
        decimal_t abs_diff = decimal_abs(diff);
        decimal_t threshold;
        decimal_from_double(0.05, 4, &threshold);
        if (decimal_cmp(abs_diff, threshold) <= 0) {
            percentage_t adj;
            decimal_add(pcts[max_idx].percent, diff, &adj.percent);
            pcts[max_idx] = adj;
        }
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", period);
    csilk_json_t* items = csilk_json_array();
    for (int k = 0; k < cat_count; k++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "name", cats[k].name);
        csilk_json_add_number(item, "amount", money_to_double(cats[k].amount));
        csilk_json_add_number(item, "pct", percentage_to_double(pcts[k]));
        csilk_json_array_append(items, item);
    }
    csilk_json_add_array(resp, "items", items);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}

void
report_expense_tag(void* pool, const report_query_t* q, report_result_t* out_res)
{
    if (!pool || !q || !out_res) {
        if (out_res) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        }
        return;
    }
    memset(out_res, 0, sizeof(*out_res));

    const char* year_str = q->year;
    const char* month_str = q->month;
    char        year_buf[8] = {0};
    time_t      now = time(NULL);
    struct tm   tm_local;
    strftime(year_buf, sizeof(year_buf), "%Y", localtime_r(&now, &tm_local));
    char period[16], period_pattern[32];
    if (year_str && month_str) {
        snprintf(period, sizeof(period), "%s-%02d-", year_str, atoi(month_str));
    } else {
        snprintf(period, sizeof(period), "%s-", year_buf);
    }
    snprintf(period_pattern, sizeof(period_pattern), "%s%%", period);

    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)q->user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)q->ledger_id);
    const char* params[] = {uid_str, lid_str, period_pattern, NULL};

    csilk_json_t* rows = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT t.name as tag_name, de.amount, de.currency "
        "FROM daily_expenses de JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id AND t.user_id=de.user_id "
        "WHERE de.user_id=? AND de.ledger_id=? AND de.expense_type='expense' AND "
        "CAST(de.expense_date AS TEXT) "
        "LIKE ? ",
        params);
    if (!rows) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询失败");
        return;
    }

    currency_t base_cur = currency_from_str("CNY");
    typedef struct {
        char    name[128];
        money_t amount;
        int64_t count;
    } tag_agg_t;

    tag_agg_t tags[128];
    int       tag_count = 0;
    money_t   total = money_zero(base_cur);

    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        const char*   name = csilk_json_get_string(row, "tag_name");
        const char*   cur_s = csilk_json_get_string(row, "currency");
        currency_t    cur = currency_from_str(cur_s ? cur_s : "CNY");
        money_t       amt = exchange_rate_convert_m(db_get_money(row, "amount", cur), base_cur);
        money_t       acc;
        if (money_add(total, amt, &acc) == DECIMAL_OK) {
            total = acc;
        }
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
            if (money_add(tags[found].amount, amt, &acc) == DECIMAL_OK) {
                tags[found].amount = acc;
            }
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

    for (int i = 0; i < tag_count - 1; i++) {
        for (int j = i + 1; j < tag_count; j++) {
            if (money_cmp(tags[j].amount, tags[i].amount) > 0) {
                tag_agg_t tmp = tags[i];
                tags[i] = tags[j];
                tags[j] = tmp;
            }
        }
    }

    percentage_t pcts[128];
    percentage_t pct_sum = percentage_zero();
    int          max_idx = 0;
    money_t      max_amt = money_zero(base_cur);
    for (int k = 0; k < tag_count; k++) {
        percentage_t pct;
        if (decimal_is_zero(total.amount)) {
            pct = percentage_zero();
        } else if (percentage_calc(tags[k].amount, total, 2, ROUND_HALF_UP, &pct) != DECIMAL_OK) {
            pct = percentage_zero();
        }
        pcts[k] = pct;
        decimal_add(pct_sum.percent, pct.percent, &pct_sum.percent);
        if (money_cmp(tags[k].amount, max_amt) > 0) {
            max_amt = tags[k].amount;
            max_idx = k;
        }
    }

    /* 尾差补偿：若百分比合计与 100% 的偏差在 ±0.05 以内，补到最大项使合计闭合 */
    if (!decimal_is_zero(total.amount) && tag_count > 0) {
        decimal_t hundred = decimal_from_int(100);
        decimal_t diff;
        decimal_sub(hundred, pct_sum.percent, &diff);
        decimal_t abs_diff = decimal_abs(diff);
        decimal_t threshold;
        decimal_from_double(0.05, 4, &threshold);
        if (decimal_cmp(abs_diff, threshold) <= 0) {
            percentage_t adj;
            decimal_add(pcts[max_idx].percent, diff, &adj.percent);
            pcts[max_idx] = adj;
        }
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "period", period);
    csilk_json_t* items = csilk_json_array();
    for (int k = 0; k < tag_count; k++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "tag_name", tags[k].name);
        csilk_json_add_number(item, "amount", money_to_double(tags[k].amount));
        csilk_json_add_number(item, "count", (double)tags[k].count);
        csilk_json_add_number(item, "pct", percentage_to_double(pcts[k]));
        csilk_json_array_append(items, item);
    }
    csilk_json_add_array(resp, "items", items);

    out_res->code = 0;
    out_res->data_payload = resp;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
}
