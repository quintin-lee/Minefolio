#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void daily_expenses_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_db_pool_t* pool = db_get_pool();
    const char* type = csilk_get_query(c, "expense_type");
    const char* cat_id = csilk_get_query(c, "category_id");
    const char* start = csilk_get_query(c, "start_date");
    const char* end = csilk_get_query(c, "end_date");

    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT de.id, de.user_id, de.category_id, de.expense_type, de.amount, "
        "de.currency, de.expense_date, de.note, de.created_at, de.updated_at, "
        "c.name as category_name "
        "FROM daily_expenses de LEFT JOIN categories c ON de.category_id=c.id "
        "WHERE de.user_id=%lld", (long long)user_id);

    if (type)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND de.expense_type='%s'", type);
    if (cat_id)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND de.category_id=%s", cat_id);
    if (start)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND de.expense_date >= '%s'", start);
    if (end)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND de.expense_date <= '%s'", end);
    snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " ORDER BY de.expense_date DESC");

    csilk_json_t* result = csilk_db_query_json(pool, sql);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
}

void daily_expenses_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
    const char* type = csilk_json_get_string(body, "expense_type");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "expense_date");

    if (category_id <= 0 || !type || amount <= 0 || !date) {
        csilk_json_free(body);
        respond_bad_request(c, "category_id、expense_type、amount、expense_date 为必填");
        return;
    }

    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";
    const char* note = csilk_json_get_string(body, "note");
    csilk_json_t* tags = csilk_json_get(body, "tags");

    csilk_db_pool_t* pool = db_get_pool();
    char sql[512];
    snprintf(sql, sizeof(sql),
        "INSERT INTO daily_expenses (user_id, category_id, expense_type, amount, currency, expense_date, note) "
        "VALUES (%lld, %lld, '%s', %.2f, '%s', '%s', '%s')",
        (long long)user_id, (long long)category_id, type, amount, currency, date, note ? note : "");

    if (csilk_db_exec(pool, sql) != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }

    // Handle tags
    if (tags && csilk_json_is_array(tags)) { // CSILK_JSON_ARRAY
        size_t n = csilk_json_array_size(tags);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* tag = csilk_json_array_get(tags, i);
            int64_t tag_id = (int64_t)csilk_json_get_number(tag, "id");
            if (tag_id <= 0) continue;

            char tag_sql[256];
            snprintf(tag_sql, sizeof(tag_sql),
                "INSERT OR IGNORE INTO expense_tags (expense_id, tag_id) "
                "VALUES ((SELECT id FROM daily_expenses ORDER BY id DESC LIMIT 1), %lld)",
                (long long)tag_id);
            csilk_db_exec(pool, tag_sql);
        }
    }

    csilk_json_free(body);
    respond_ok_null(c);
}

void daily_expenses_update(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    csilk_db_pool_t* pool = db_get_pool();

    char check_sql[256];
    snprintf(check_sql, sizeof(check_sql),
        "SELECT id FROM daily_expenses WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_json_t* chk = csilk_db_query_json(pool, check_sql);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
    const char* type = csilk_json_get_string(body, "expense_type");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "expense_date");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* note = csilk_json_get_string(body, "note");

    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE daily_expenses SET category_id=%lld, expense_type='%s', amount=%.2f, "
        "currency='%s', expense_date='%s', note='%s', updated_at=CURRENT_TIMESTAMP "
        "WHERE id=%s AND user_id=%lld",
        (long long)category_id, type ? type : "", amount,
        currency ? currency : "CNY", date ? date : "", note ? note : "",
        id_str, (long long)user_id);

    csilk_db_exec(pool, sql);
    csilk_json_free(body);
    respond_ok_null(c);
}

void daily_expenses_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char del_tags_sql[256];
    snprintf(del_tags_sql, sizeof(del_tags_sql),
        "DELETE FROM expense_tags WHERE expense_id=%s", id_str);
    csilk_db_exec(pool, del_tags_sql);

    char del_sql[256];
    snprintf(del_sql, sizeof(del_sql),
        "DELETE FROM daily_expenses WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_db_exec(pool, del_sql);
    respond_ok_null(c);
}

void daily_expenses_monthly(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");
    if (!year_str || !month_str) {
        respond_bad_request(c, "year 和 month 参数为必填");
        return;
    }

    char date_prefix[16];
    snprintf(date_prefix, sizeof(date_prefix), "%s-%s-", year_str, month_str);
    csilk_db_pool_t* pool = db_get_pool();

    // Totals
    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT "
        "  COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END), 0) as total_income, "
        "  COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END), 0) as total_expense "
        "FROM daily_expenses "
        "WHERE user_id=%lld AND expense_date LIKE '%s%%'",
        (long long)user_id, date_prefix);
    csilk_json_t* totals = csilk_db_query_json(pool, sql);
    double income = 0, expense = 0;
    if (totals && csilk_json_array_size(totals) > 0) {
        income = csilk_json_get_number(csilk_json_array_get(totals, 0), "total_income");
        expense = csilk_json_get_number(csilk_json_array_get(totals, 0), "total_expense");
    }
    if (totals) csilk_json_free(totals);

    // By category
    snprintf(sql, sizeof(sql),
        "SELECT c.name as category_name, de.expense_type, SUM(de.amount) as amount "
        "FROM daily_expenses de JOIN categories c ON de.category_id=c.id "
        "WHERE de.user_id=%lld AND de.expense_date LIKE '%s%%' "
        "GROUP BY c.name, de.expense_type ORDER BY amount DESC",
        (long long)user_id, date_prefix);
    csilk_json_t* by_cat = csilk_db_query_json(pool, sql);

    // By tag
    snprintf(sql, sizeof(sql),
        "SELECT t.name as tag_name, SUM(de.amount) as amount, COUNT(*) as count "
        "FROM daily_expenses de "
        "JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id "
        "WHERE de.user_id=%lld AND de.expense_date LIKE '%s%%' "
        "GROUP BY t.name ORDER BY amount DESC",
        (long long)user_id, date_prefix);
    csilk_json_t* by_tag = csilk_db_query_json(pool, sql);

    // Daily breakdown
    snprintf(sql, sizeof(sql),
        "SELECT expense_date, "
        "  COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END), 0) as income, "
        "  COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END), 0) as expense "
        "FROM daily_expenses "
        "WHERE user_id=%lld AND expense_date LIKE '%s%%' "
        "GROUP BY expense_date ORDER BY expense_date",
        (long long)user_id, date_prefix);
    csilk_json_t* daily = csilk_db_query_json(pool, sql);

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
