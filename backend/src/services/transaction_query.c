#include "services/transaction_service.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
transactions_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);

    csilk_db_pool_t* pool = db_get_pool();
    const char*      asset_id = csilk_get_query(c, "asset_id");
    const char*      category_id = csilk_get_query(c, "category_id");
    const char*      type = csilk_get_query(c, "type");
    if (!type || strlen(type) == 0) {
        type = csilk_get_query(c, "transaction_type");
    }
    const char* start_date = csilk_get_query(c, "start_date");
    const char* end_date = csilk_get_query(c, "end_date");
    const char* source_type = csilk_get_query(c, "source_type");

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    char sql[1024];
    snprintf(sql,
             sizeof(sql),
             "SELECT t.id, t.asset_id, t.linked_asset_id, t.category_id, t.transaction_type, "
             "t.source_type, "
             "t.direction, t.linked_direction, t.amount, "
             "t.price_per_unit, t.quantity, t.currency, t.transaction_date, t.note, "
             "a.name as asset_name, la.name as linked_asset_name, c.name as category_name "
             "FROM transactions t "
             "LEFT JOIN assets a ON t.asset_id=a.id "
             "LEFT JOIN assets la ON t.linked_asset_id=la.id "
             "LEFT JOIN categories c ON t.category_id=c.id "
             "WHERE t.user_id=?");

    char count_sql[1024];
    snprintf(count_sql,
             sizeof(count_sql),
             "SELECT COUNT(*) AS cnt FROM transactions t WHERE t.user_id=?");

    const char* params[16];
    params[0] = uid_str;
    int pidx = 1;

    if (asset_id) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.asset_id=?");
        snprintf(count_sql + strlen(count_sql),
                 sizeof(count_sql) - strlen(count_sql),
                 " AND t.asset_id=?");
        params[pidx++] = asset_id;
    }
    if (category_id) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.category_id=?");
        snprintf(count_sql + strlen(count_sql),
                 sizeof(count_sql) - strlen(count_sql),
                 " AND t.category_id=?");
        params[pidx++] = category_id;
    }
    if (type) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_type=?");
        snprintf(count_sql + strlen(count_sql),
                 sizeof(count_sql) - strlen(count_sql),
                 " AND t.transaction_type=?");
        params[pidx++] = type;
    }
    if (source_type) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.source_type=?");
        snprintf(count_sql + strlen(count_sql),
                 sizeof(count_sql) - strlen(count_sql),
                 " AND t.source_type=?");
        params[pidx++] = source_type;
    }
    if (start_date) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_date >= ?");
        snprintf(count_sql + strlen(count_sql),
                 sizeof(count_sql) - strlen(count_sql),
                 " AND t.transaction_date >= ?");
        params[pidx++] = start_date;
    }
    if (end_date) {
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_date <= ?");
        snprintf(count_sql + strlen(count_sql),
                 sizeof(count_sql) - strlen(count_sql),
                 " AND t.transaction_date <= ?");
        params[pidx++] = end_date;
    }
    snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " ORDER BY t.transaction_date DESC");

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
    int64_t       total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
        total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    }
    if (cnt_res) {
        csilk_json_free(cnt_res);
    }
    params[pidx_count] = limit_buf;

    csilk_json_t* result = csilk_db_query_param_json(pool, sql, params);
    if (!result) {
        respond_error(c, 500, "查询失败");
        return;
    }
    respond_page_ok(c, result, total, page, page_size);
}

void
transactions_monthly(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* month = csilk_get_query(c, "month");
    if (!month || strlen(month) != 7 || month[4] != '-') {
        respond_bad_request(c, "month 参数格式错误 (YYYY-MM)");
        return;
    }
    for (int i = 0; i < 7; i++) {
        if (i != 4 && (month[i] < '0' || month[i] > '9')) {
            respond_bad_request(c, "month 参数格式错误 (YYYY-MM)");
            return;
        }
    }

    char uid_str[32], pattern[16];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(pattern, sizeof(pattern), "%.4s-%.2s%%", month, month + 5);

    const char*      params[] = {uid_str, pattern, NULL};
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    res = csilk_db_query_param_json(
        pool,
        "SELECT "
        "  COALESCE(SUM(amount), 0) AS total_volume, "
        "  COALESCE(SUM(CASE WHEN direction='in' THEN amount ELSE 0 END), 0) AS inflows, "
        "  COALESCE(SUM(CASE WHEN direction='out' THEN amount ELSE 0 END), 0) AS outflows, "
        "  COUNT(*) AS count "
        "FROM transactions WHERE user_id=? AND transaction_date LIKE ?",
        params);
    if (!res) {
        respond_error(c, 500, "查询失败");
        return;
    }

    csilk_json_t* resp = csilk_json_object();
    if (csilk_json_array_size(res) > 0) {
        const csilk_json_t* row = csilk_json_array_get(res, 0);
        csilk_json_add_number(resp, "total_volume", db_get_num(row, "total_volume"));
        csilk_json_add_number(resp, "inflows", db_get_num(row, "inflows"));
        csilk_json_add_number(resp, "outflows", db_get_num(row, "outflows"));
        csilk_json_add_number(resp, "count", db_get_num(row, "count"));
    } else {
        csilk_json_add_number(resp, "total_volume", 0);
        csilk_json_add_number(resp, "inflows", 0);
        csilk_json_add_number(resp, "outflows", 0);
        csilk_json_add_number(resp, "count", 0);
    }
    csilk_json_free(res);
    respond_ok(c, resp);
}
