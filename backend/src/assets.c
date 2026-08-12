#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void assets_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_db_pool_t* pool = db_get_pool();
    const char* cat_id = csilk_get_query(c, "category_id");

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* result = NULL;
    if (cat_id && strlen(cat_id) > 0) {
        const char* params[] = { uid_str, cat_id, NULL };
        result = csilk_db_query_param_json(pool,
            "SELECT a.id, a.name, a.account_no, a.current_value, a.currency, "
            "a.note, a.created_at, a.updated_at, c.name as category_name, c.asset_type "
            "FROM assets a LEFT JOIN categories c ON a.category_id=c.id "
            "WHERE a.user_id=? AND a.category_id=? ORDER BY a.name", params);
    } else {
        const char* params[] = { uid_str, NULL };
        result = csilk_db_query_param_json(pool,
            "SELECT a.id, a.name, a.account_no, a.current_value, a.currency, "
            "a.note, a.created_at, a.updated_at, c.name as category_name, c.asset_type "
            "FROM assets a LEFT JOIN categories c ON a.category_id=c.id "
            "WHERE a.user_id=? ORDER BY c.name, a.name", params);
    }

    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
}

void assets_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    int64_t category_id = db_get_int(body, "category_id");
    if (!name || category_id <= 0) {
        csilk_json_free(body);
        respond_bad_request(c, "name 和 category_id 为必填");
        return;
    }

    const char* account_no = csilk_json_get_string(body, "account_no");
    double value = db_get_num(body, "current_value");
    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";
    const char* note = csilk_json_get_string(body, "note");

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32], cat_str[32], val_str[64];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)category_id);
    snprintf(val_str, sizeof(val_str), "%.6f", value);

    const char* params[] = {
        uid_str, cat_str, name, account_no ? account_no : "", val_str, currency, note ? note : "", NULL
    };
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "INSERT INTO assets (user_id, category_id, name, account_no, current_value, currency, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)", params);

    if (!res) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_free(res);
    csilk_json_free(body);
    respond_ok_null(c);
}

void assets_update(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    // Verify ownership
    const char* chk_params[] = { id_str, uid_str, NULL };
    csilk_json_t* chk = csilk_db_query_param_json(pool,
        "SELECT id FROM assets WHERE id=? AND user_id=?", chk_params);
    if (!chk || csilk_json_array_size(chk) == 0) {
        csilk_json_free(body);
        if (chk) csilk_json_free(chk);
        respond_not_found(c);
        return;
    }
    csilk_json_free(chk);

    const char* name = csilk_json_get_string(body, "name");
    const char* account_no = csilk_json_get_string(body, "account_no");
    double value = db_get_num(body, "current_value");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* note = csilk_json_get_string(body, "note");

    char val_str[64];
    snprintf(val_str, sizeof(val_str), "%.6f", value);

    const char* params[] = {
        name ? name : "", account_no ? account_no : "", val_str,
        currency ? currency : "CNY", note ? note : "", id_str, uid_str, NULL
    };
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "UPDATE assets SET name=?, account_no=?, current_value=?, currency=?, note=?, "
        "updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=?", params);
    if (res) csilk_json_free(res);

    csilk_json_free(body);
    respond_ok_null(c);
}

void assets_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { id_str, uid_str, NULL };

    csilk_json_t* res = csilk_db_query_param_json(pool,
        "DELETE FROM assets WHERE id=? AND user_id=?", params);
    if (!res) {
        respond_error(c, 500, "删除失败");
        return;
    }
    csilk_json_free(res);
    respond_ok_null(c);
}

void assets_detail(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { id_str, uid_str, NULL };

    csilk_json_t* result = csilk_db_query_param_json(pool,
        "SELECT a.id, a.name, a.account_no, a.current_value, a.currency, "
        "a.note, a.created_at, a.updated_at, c.name as category_name, c.asset_type "
        "FROM assets a LEFT JOIN categories c ON a.category_id=c.id "
        "WHERE a.id=? AND a.user_id=?", params);

    if (!result || csilk_json_array_size(result) == 0) {
        respond_not_found(c);
        if (result) csilk_json_free(result);
        return;
    }

    csilk_json_t* row = csilk_json_array_get(result, 0);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", db_get_num(row, "id"));
    csilk_json_add_string(resp, "name", csilk_json_get_string(row, "name"));
    csilk_json_add_string(resp, "account_no", csilk_json_get_string(row, "account_no"));
    csilk_json_add_number(resp, "current_value", db_get_num(row, "current_value"));
    csilk_json_add_string(resp, "currency", csilk_json_get_string(row, "currency"));
    csilk_json_add_string(resp, "note", csilk_json_get_string(row, "note"));
    csilk_json_add_string(resp, "category_name", csilk_json_get_string(row, "category_name"));
    csilk_json_add_string(resp, "asset_type", csilk_json_get_string(row, "asset_type"));
    csilk_json_add_string(resp, "created_at", csilk_json_get_string(row, "created_at"));
    csilk_json_add_string(resp, "updated_at", csilk_json_get_string(row, "updated_at"));

    // 历史交易 (spec §4.4: 资产详情 + 历史交易)
    csilk_json_t* tx_rows = csilk_db_query_param_json(pool,
        "SELECT id, asset_id, transaction_type, amount, quantity, price_per_unit, "
        "currency, transaction_date, note, created_at "
        "FROM transactions WHERE asset_id=? AND user_id=? "
        "ORDER BY transaction_date DESC", params);
    csilk_json_t* transactions = csilk_json_array();
    if (tx_rows) {
        size_t n = csilk_json_array_size(tx_rows);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* tr = csilk_json_array_get(tx_rows, i);
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_number(item, "id", db_get_num(tr, "id"));
            csilk_json_add_number(item, "asset_id", db_get_num(tr, "asset_id"));
            csilk_json_add_string(item, "transaction_type", csilk_json_get_string(tr, "transaction_type"));
            csilk_json_add_number(item, "amount", db_get_num(tr, "amount"));
            csilk_json_add_number(item, "quantity", db_get_num(tr, "quantity"));
            csilk_json_add_number(item, "price_per_unit", db_get_num(tr, "price_per_unit"));
            csilk_json_add_string(item, "currency", csilk_json_get_string(tr, "currency"));
            csilk_json_add_string(item, "transaction_date", csilk_json_get_string(tr, "transaction_date"));
            csilk_json_add_string(item, "note", csilk_json_get_string(tr, "note"));
            csilk_json_add_string(item, "created_at", csilk_json_get_string(tr, "created_at"));
            csilk_json_array_append(transactions, item);
        }
        csilk_json_free(tx_rows);
    }
    csilk_json_add_array(resp, "transactions", transactions);
    csilk_json_free(result);

    respond_ok(c, resp);
}
