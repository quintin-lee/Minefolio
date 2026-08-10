#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assets_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_db_pool_t* pool = db_get_pool();
    const char* cat_id = csilk_get_query(c, "category_id");

    char sql[512];
    if (cat_id) {
        snprintf(sql, sizeof(sql),
            "SELECT a.id, a.name, a.account_no, a.current_value, a.currency, "
            "a.note, a.created_at, a.updated_at, c.name as category_name, c.asset_type "
            "FROM assets a LEFT JOIN categories c ON a.category_id=c.id "
            "WHERE a.user_id=%lld AND a.category_id=%s ORDER BY a.name",
            (long long)user_id, cat_id);
    } else {
        snprintf(sql, sizeof(sql),
            "SELECT a.id, a.name, a.account_no, a.current_value, a.currency, "
            "a.note, a.created_at, a.updated_at, c.name as category_name, c.asset_type "
            "FROM assets a LEFT JOIN categories c ON a.category_id=c.id "
            "WHERE a.user_id=%lld ORDER BY c.name, a.name",
            (long long)user_id);
    }

    csilk_json_t* result = csilk_db_query_json(pool, sql);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
}

static void assets_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
    if (!name || category_id <= 0) {
        csilk_json_free(body);
        respond_bad_request(c, "name 和 category_id 为必填");
        return;
    }

    const char* account_no = csilk_json_get_string(body, "account_no");
    double value = csilk_json_get_number(body, "current_value");
    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";
    const char* note = csilk_json_get_string(body, "note");

    csilk_db_pool_t* pool = db_get_pool();
    char sql[512];
    snprintf(sql, sizeof(sql),
        "INSERT INTO assets (user_id, category_id, name, account_no, current_value, currency, note) "
        "VALUES (%lld, %lld, '%s', '%s', %.2f, '%s', '%s')",
        (long long)user_id, (long long)category_id,
        name, account_no ? account_no : "", value, currency, note ? note : "");

    if (csilk_db_exec(pool, sql) != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_free(body);
    respond_ok_null(c);
}

static void assets_update(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    csilk_db_pool_t* pool = db_get_pool();

    // Verify ownership
    char check_sql[256];
    snprintf(check_sql, sizeof(check_sql),
        "SELECT id FROM assets WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_json_t* chk = csilk_db_query_json(pool, check_sql);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    const char* name = csilk_json_get_string(body, "name");
    const char* account_no = csilk_json_get_string(body, "account_no");
    double value = csilk_json_get_number(body, "current_value");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* note = csilk_json_get_string(body, "note");

    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE assets SET name='%s', account_no='%s', current_value=%.2f, currency='%s', note='%s', "
        "updated_at=CURRENT_TIMESTAMP WHERE id=%s AND user_id=%lld",
        name ? name : "", account_no ? account_no : "", value,
        currency ? currency : "CNY", note ? note : "",
        id_str, (long long)user_id);

    csilk_db_exec(pool, sql);
    csilk_json_free(body);
    respond_ok_null(c);
}

static void assets_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char sql[256];
    snprintf(sql, sizeof(sql),
        "DELETE FROM assets WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_db_exec(pool, sql);
    respond_ok_null(c);
}

static void assets_detail(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT a.id, a.name, a.account_no, a.current_value, a.currency, "
        "a.note, a.created_at, a.updated_at, c.name as category_name, c.asset_type "
        "FROM assets a LEFT JOIN categories c ON a.category_id=c.id "
        "WHERE a.id=%s AND a.user_id=%lld", id_str, (long long)user_id);

    csilk_json_t* result = csilk_db_query_json(pool, sql);
    if (!result || csilk_json_array_size(result) == 0) {
        respond_not_found(c);
        if (result) csilk_json_free(result);
        return;
    }
    respond_ok(c, result);
}
