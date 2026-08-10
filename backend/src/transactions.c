#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void transactions_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_db_pool_t* pool = db_get_pool();
    const char* asset_id = csilk_get_query(c, "asset_id");
    const char* category_id = csilk_get_query(c, "category_id");
    const char* type = csilk_get_query(c, "type");
    const char* start_date = csilk_get_query(c, "start_date");
    const char* end_date = csilk_get_query(c, "end_date");

    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT t.id, t.asset_id, t.category_id, t.transaction_type, t.amount, "
        "t.price_per_unit, t.quantity, t.currency, t.transaction_date, t.note, "
        "a.name as asset_name, c.name as category_name "
        "FROM transactions t "
        "LEFT JOIN assets a ON t.asset_id=a.id "
        "LEFT JOIN categories c ON t.category_id=c.id "
        "WHERE t.user_id=%lld", (long long)user_id);

    if (asset_id)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.asset_id=%s", asset_id);
    if (category_id)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.category_id=%s", category_id);
    if (type)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_type='%s'", type);
    if (start_date)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_date >= '%s'", start_date);
    if (end_date)
        snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " AND t.transaction_date <= '%s'", end_date);
    snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), " ORDER BY t.transaction_date DESC");

    csilk_json_t* result = csilk_db_query_json(pool, sql);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
}

static void transactions_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    int64_t asset_id = (int64_t)csilk_json_get_number(body, "asset_id");
    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
    const char* type = csilk_json_get_string(body, "transaction_type");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "transaction_date");

    if (asset_id <= 0 || !type || amount <= 0 || !date) {
        csilk_json_free(body);
        respond_bad_request(c, "asset_id、transaction_type、amount、transaction_date 为必填");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    // Verify asset belongs to user
    char check_sql[256];
    snprintf(check_sql, sizeof(check_sql),
        "SELECT id FROM assets WHERE id=%lld AND user_id=%lld",
        (long long)asset_id, (long long)user_id);
    csilk_json_t* chk = csilk_db_query_json(pool, check_sql);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";
    const char* note = csilk_json_get_string(body, "note");
    double price = csilk_json_get_number(body, "price_per_unit");
    double qty = csilk_json_get_number(body, "quantity");

    char sql[512];
    snprintf(sql, sizeof(sql),
        "INSERT INTO transactions (user_id, asset_id, category_id, transaction_type, "
        "amount, price_per_unit, quantity, currency, transaction_date, note) "
        "VALUES (%lld, %lld, %lld, '%s', %.6f, %.4f, %.4f, '%s', '%s', '%s')",
        (long long)user_id, (long long)asset_id, (long long)category_id,
        type, amount, price, qty, currency, date, note ? note : "");

    if (csilk_db_exec(pool, sql) != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_free(body);
    respond_ok_null(c);
}

static void transactions_update(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    csilk_db_pool_t* pool = db_get_pool();

    char check_sql[256];
    snprintf(check_sql, sizeof(check_sql),
        "SELECT id FROM transactions WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_json_t* chk = csilk_db_query_json(pool, check_sql);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    const char* type = csilk_json_get_string(body, "transaction_type");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "transaction_date");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* note = csilk_json_get_string(body, "note");
    double price = csilk_json_get_number(body, "price_per_unit");
    double qty = csilk_json_get_number(body, "quantity");

    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE transactions SET transaction_type='%s', amount=%.6f, price_per_unit=%.4f, "
        "quantity=%.4f, currency='%s', transaction_date='%s', note='%s', "
        "updated_at=CURRENT_TIMESTAMP WHERE id=%s AND user_id=%lld",
        type ? type : "", amount, price, qty,
        currency ? currency : "CNY", date ? date : "", note ? note : "",
        id_str, (long long)user_id);

    csilk_db_exec(pool, sql);
    csilk_json_free(body);
    respond_ok_null(c);
}

static void transactions_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char sql[256];
    snprintf(sql, sizeof(sql),
        "DELETE FROM transactions WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_db_exec(pool, sql);
    respond_ok_null(c);
}
