#include "services/daily_expense_service.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "common/balance.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void daily_expenses_list(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    csilk_db_pool_t* pool = db_get_pool();
    const char* type = csilk_get_query(c, "expense_type");
    const char* cat_id = csilk_get_query(c, "category_id");
    const char* tag_ids = csilk_get_query(c, "tag_ids");
    const char* start = csilk_get_query(c, "start_date");
    const char* end = csilk_get_query(c, "end_date");

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT de.id, de.user_id, de.category_id, de.asset_id, de.expense_type, de.amount, "
        "de.currency, de.expense_date, de.note, de.created_at, de.updated_at, "
        "c.name as category_name, a.name as asset_name, "
        "(SELECT json_group_array(json_object('id', t.id, 'name', t.name, 'color', t.color)) "
        " FROM expense_tags et JOIN tags t ON et.tag_id=t.id "
        " WHERE et.expense_id=de.id) as tags "
        "FROM daily_expenses de "
        "LEFT JOIN categories c ON de.category_id=c.id "
        "LEFT JOIN assets a ON de.asset_id=a.id "
        "WHERE de.user_id=?");

    char count_sql[1024];
    snprintf(count_sql, sizeof(count_sql),
        "SELECT COUNT(*) AS cnt FROM daily_expenses de WHERE de.user_id=?");

    const char* params[16];
    params[0] = uid_str;
    int pidx = 1;

    if (type) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND de.expense_type=?");
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql), " AND de.expense_type=?");
        params[pidx++] = type;
    }
    if (cat_id) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND de.category_id=?");
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql), " AND de.category_id=?");
        params[pidx++] = cat_id;
    }
    if (tag_ids && tag_ids[0]) {
        /* Parse comma-separated tag IDs into parameterised placeholders.
         * Each parsed integer is snprintf'd into its own buffer so the
         * pointer stays valid for the params array — avoids string concat
         * into the SQL body entirely. */
        char tag_bufs[32][32];
        const char* tag_ptrs[32];
        int tag_count = 0;
        size_t pos = 0;
        while (tag_ids[pos]) {
            while (tag_ids[pos] == ',' || tag_ids[pos] == ' ') pos++;
            if (!tag_ids[pos]) break;
            size_t start = pos;
            while (tag_ids[pos] && tag_ids[pos] != ',' && tag_ids[pos] != ' ') pos++;
            size_t len = pos - start;
            if (len == 0 || len >= sizeof(tag_bufs[0])) {
                respond_bad_request(c, "tag_ids 格式错误"); return;
            }
            int ok = 1;
            if (len == 1 && tag_ids[start] == '0') ok = 0;
            for (size_t k = 0; k < len && ok; k++)
                if (tag_ids[start + k] < '1' || tag_ids[start + k] > '9') ok = 0;
            if (!ok) { respond_bad_request(c, "tag_ids 只能包含正整数"); return; }
            if (tag_count >= 32) { respond_bad_request(c, "tag_ids 最多支持 32 个"); return; }
            memcpy(tag_bufs[tag_count], tag_ids + start, len);
            tag_bufs[tag_count][len] = '\0';
            tag_ptrs[tag_count++] = tag_bufs[tag_count];
        }
        char in_clause[512];
        int ipos = 0;
        for (int i = 0; i < tag_count; i++) {
            if (i > 0) in_clause[ipos++] = ',';
            ipos += snprintf(in_clause + ipos, sizeof(in_clause) - (size_t)ipos, " ?");
        }
        char filter[512];
        snprintf(filter, sizeof(filter),
            " AND EXISTS (SELECT 1 FROM expense_tags et2 "
            " WHERE et2.expense_id=de.id AND et2.tag_id IN (%s))",
            in_clause);
        if ((size_t)(strlen(sql) + strlen(filter) + 1) >= sizeof(sql) ||
            (size_t)(strlen(count_sql) + strlen(filter) + 1) >= sizeof(count_sql)) {
            respond_error(c, 500, "查询过长"); return;
        }
        strncat(sql,      filter, sizeof(sql)      - strlen(sql)      - 1);
        strncat(count_sql, filter, sizeof(count_sql) - strlen(count_sql) - 1);
        for (int i = 0; i < tag_count && pidx < 15; i++) params[pidx++] = tag_ptrs[i];
    }
    if (start) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND de.expense_date >= ?");
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql), " AND de.expense_date >= ?");
        params[pidx++] = start;
    }
    if (end) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND de.expense_date <= ?");
        snprintf(count_sql + strlen(count_sql), sizeof(count_sql) - strlen(count_sql), " AND de.expense_date <= ?");
        params[pidx++] = end;
    }
    snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " ORDER BY de.expense_date DESC");

    int pidx_count = pidx; // count query ends before pagination params

    char limit_buf[32], offset_buf[32];
    snprintf(limit_buf, sizeof(limit_buf), "%lld", (long long)page_size);
    snprintf(offset_buf, sizeof(offset_buf), "%lld", (long long)((page - 1) * page_size));
    snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " LIMIT ? OFFSET ?");
    params[pidx++] = limit_buf;
    params[pidx++] = offset_buf;
    params[pidx] = NULL;

    params[pidx_count] = NULL;
    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, params);
    int64_t total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) csilk_json_free(cnt_res);
    params[pidx_count] = limit_buf;

    csilk_json_t* result = csilk_db_query_param_json(pool, sql, params);
    if (!result) { respond_error(c, 500, "查询失败"); return; }

    csilk_json_t* list = csilk_json_array();
    size_t n = csilk_json_array_size(result);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(result, i);
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_number(item, "id", db_get_num(row, "id"));
        csilk_json_add_number(item, "user_id", db_get_num(row, "user_id"));
        csilk_json_add_number(item, "category_id", db_get_num(row, "category_id"));
        csilk_json_add_string(item, "expense_type", csilk_json_get_string(row, "expense_type"));
        csilk_json_add_number(item, "amount", db_get_num(row, "amount"));
        csilk_json_add_string(item, "currency", csilk_json_get_string(row, "currency"));
        csilk_json_add_string(item, "expense_date", csilk_json_get_string(row, "expense_date"));
        csilk_json_add_string(item, "note", csilk_json_get_string(row, "note"));
        csilk_json_add_string(item, "created_at", csilk_json_get_string(row, "created_at"));
        csilk_json_add_string(item, "updated_at", csilk_json_get_string(row, "updated_at"));
        csilk_json_add_string(item, "category_name", csilk_json_get_string(row, "category_name"));
        csilk_json_add_number(item, "asset_id", db_get_num(row, "asset_id"));
        csilk_json_add_string(item, "asset_name", csilk_json_get_string(row, "asset_name"));
        const char* tags_str = csilk_json_get_string(row, "tags");
        csilk_json_t* tags_arr = (tags_str && tags_str[0]) ? csilk_json_parse(tags_str) : NULL;
        csilk_json_add_array(item, "tags", tags_arr ? tags_arr : csilk_json_array());
        csilk_json_array_append(list, item);
    }
    csilk_json_free(result);
    respond_page_ok(c, list, total, page, page_size);
}
void daily_expenses_monthly(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");
    if (!year_str || !month_str) {
        respond_bad_request(c, "year 和 month 参数为必填");
        return;
    }

    char date_pattern[32];
    snprintf(date_pattern, sizeof(date_pattern), "%s-%02d-%%", year_str, atoi(month_str));

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, date_pattern, NULL };

    csilk_db_pool_t* pool = db_get_pool();

    csilk_json_t* totals = csilk_db_query_param_json(pool,
        "SELECT "
        "  COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END), 0) as total_income, "
        "  COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END), 0) as total_expense "
        "FROM daily_expenses "
        "WHERE user_id=? AND expense_date LIKE ?", params);
    double income = 0, expense = 0;
    if (totals && csilk_json_array_size(totals) > 0) {
        const csilk_json_t* tot_row = csilk_json_array_get(totals, 0);
        income = db_get_num(tot_row, "total_income");
        expense = db_get_num(tot_row, "total_expense");
    }
    if (totals) csilk_json_free(totals);

    csilk_json_t* by_cat = csilk_db_query_param_json(pool,
        "SELECT c.name as category_name, de.expense_type, SUM(de.amount) as amount "
        "FROM daily_expenses de JOIN categories c ON de.category_id=c.id "
        "WHERE de.user_id=? AND de.expense_date LIKE ? "
        "GROUP BY c.name, de.expense_type ORDER BY amount DESC", params);

    csilk_json_t* by_tag = csilk_db_query_param_json(pool,
        "SELECT t.name as tag_name, SUM(de.amount) as amount, COUNT(*) as count "
        "FROM daily_expenses de "
        "JOIN expense_tags et ON de.id=et.expense_id "
        "JOIN tags t ON et.tag_id=t.id "
        "WHERE de.user_id=? AND de.expense_date LIKE ?", params);

    csilk_json_t* daily = csilk_db_query_param_json(pool,
        "SELECT expense_date, "
        "  COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END), 0) as income, "
        "  COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END), 0) as expense "
        "FROM daily_expenses "
        "WHERE user_id=? AND expense_date LIKE ? "
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
