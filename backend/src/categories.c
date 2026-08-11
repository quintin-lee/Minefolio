#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static csilk_json_t* row_to_category(csilk_json_t* row) {
    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_number(obj, "id", db_get_num(row, "id"));
    csilk_json_add_string(obj, "name", csilk_json_get_string(row, "name"));
    csilk_json_add_string(obj, "parent_name", csilk_json_get_string(row, "parent_name"));
    double pid = db_get_num(row, "parent_id");
    csilk_json_add_object(obj, "parent_id",
        pid > 0 ? csilk_json_number(pid) : csilk_json_null());
    const char* type = csilk_json_get_string(row, "type");
    csilk_json_add_string(obj, "type", (type && type[0]) ? type : "asset");
    csilk_json_add_string(obj, "asset_type", csilk_json_get_string(row, "asset_type"));
    csilk_json_add_string(obj, "currency", csilk_json_get_string(row, "currency"));
    csilk_json_add_string(obj, "icon", csilk_json_get_string(row, "icon"));
    csilk_json_add_number(obj, "sort_order", db_get_num(row, "sort_order"));
    return obj;
}

static void add_children(csilk_db_pool_t* pool, csilk_json_t* parent) {
    int64_t pid = db_get_int(parent, "id");
    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT c.id, c.name, c.parent_id, "
        "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
        "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
        "FROM categories c WHERE parent_id = %lld ORDER BY c.sort_order", (long long)pid);

    csilk_json_t* kids = csilk_db_query_json(pool, sql);
    if (!kids) return;

    size_t n = csilk_json_array_size(kids);
    if (n == 0) { csilk_json_free(kids); return; }

    csilk_json_t* children = csilk_json_array();
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* kid = csilk_json_array_get(kids, i);
        csilk_json_t* kid_obj = row_to_category(kid);
        add_children(pool, kid_obj);
        csilk_json_array_append(children, kid_obj);
    }
    csilk_json_add_array(parent, "children", children);
    csilk_json_free(kids);
}

void categories_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* type_query = csilk_get_query(c, "type");
    csilk_db_pool_t* pool = db_get_pool();
    char sql[512];

    if (type_query && strlen(type_query) > 0) {
        if (strstr(type_query, ",")) {
            char in_clause[256] = {0};
            char copy[128];
            strncpy(copy, type_query, sizeof(copy)-1);
            copy[sizeof(copy)-1] = '\0';
            char* tok = strtok(copy, ",");
            int first = 1;
            while (tok) {
                while (*tok == ' ') tok++;
                if (strlen(tok) > 0) {
                    if (!first) strncat(in_clause, ",", sizeof(in_clause)-strlen(in_clause)-1);
                    strncat(in_clause, "'", sizeof(in_clause)-strlen(in_clause)-1);
                    strncat(in_clause, tok, sizeof(in_clause)-strlen(in_clause)-1);
                    strncat(in_clause, "'", sizeof(in_clause)-strlen(in_clause)-1);
                    first = 0;
                }
                tok = strtok(NULL, ",");
            }
            snprintf(sql, sizeof(sql),
                "SELECT c.id, c.name, c.parent_id, "
                "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
                "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
                "FROM categories c WHERE user_id = %lld AND parent_id IS NULL AND c.type IN (%s) ORDER BY c.sort_order",
                (long long)user_id, in_clause[0] ? in_clause : "'asset'");
        } else {
            snprintf(sql, sizeof(sql),
                "SELECT c.id, c.name, c.parent_id, "
                "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
                "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
                "FROM categories c WHERE user_id = %lld AND parent_id IS NULL AND c.type = '%s' ORDER BY c.sort_order",
                (long long)user_id, type_query);
        }
    } else {
        snprintf(sql, sizeof(sql),
            "SELECT c.id, c.name, c.parent_id, "
            "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
            "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
            "FROM categories c WHERE user_id = %lld AND parent_id IS NULL ORDER BY c.sort_order",
            (long long)user_id);
    }

    csilk_json_t* rows = csilk_db_query_json(pool, sql);
    if (!rows) { respond_error(c, 500, "查询失败"); return; }

    csilk_json_t* tree = csilk_json_array();
    size_t n = csilk_json_array_size(rows);
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(rows, i);
        csilk_json_t* node = row_to_category(row);
        add_children(pool, node);
        csilk_json_array_append(tree, node);
    }
    csilk_json_free(rows);
    respond_ok(c, tree);
}

void categories_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    const char* type = csilk_json_get_string(body, "type");
    if (!type || strlen(type) == 0) type = "asset";

    const char* asset_type = csilk_json_get_string(body, "asset_type");
    if (!asset_type || strlen(asset_type) == 0) asset_type = "cash";

    if (!name) {
        csilk_json_free(body);
        respond_bad_request(c, "name 为必填");
        return;
    }

    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) currency = "CNY";
    const char* icon = csilk_json_get_string(body, "icon");
    if (!icon) icon = "";
    int64_t parent_id = (int64_t)csilk_json_get_number(body, "parent_id");
    int sort_order = (int)csilk_json_get_number(body, "sort_order");

    csilk_db_pool_t* pool = db_get_pool();
    char sql[512];
    if (parent_id > 0) {
        snprintf(sql, sizeof(sql),
            "INSERT INTO categories (user_id, name, parent_id, type, asset_type, currency, icon, sort_order) "
            "VALUES (%lld, '%s', %lld, '%s', '%s', '%s', '%s', %d)",
            (long long)user_id, name, parent_id, type, asset_type, currency, icon, sort_order);
    } else {
        snprintf(sql, sizeof(sql),
            "INSERT INTO categories (user_id, name, type, asset_type, currency, icon, sort_order) "
            "VALUES (%lld, '%s', '%s', '%s', '%s', '%s', %d)",
            (long long)user_id, name, type, asset_type, currency, icon, sort_order);
    }

    if (csilk_db_exec(pool, sql) != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_free(body);
    respond_ok_null(c);
}

void categories_update(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    const char* type = csilk_json_get_string(body, "type");
    if (!type || strlen(type) == 0) type = "asset";

    const char* asset_type = csilk_json_get_string(body, "asset_type");
    if (!asset_type || strlen(asset_type) == 0) asset_type = "cash";

    const char* currency = csilk_json_get_string(body, "currency");
    const char* icon = csilk_json_get_string(body, "icon");
    int sort_order = (int)csilk_json_get_number(body, "sort_order");

    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE categories SET name='%s', type='%s', asset_type='%s', currency='%s', icon='%s', sort_order=%d "
        "WHERE id=%s AND user_id=%lld",
        name ? name : "", type, asset_type,
        currency ? currency : "CNY", icon ? icon : "", sort_order,
        id_str, (long long)user_id);

    csilk_db_exec(db_get_pool(), sql);
    csilk_json_free(body);
    respond_ok_null(c);
}

void categories_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();

    // Check if has children
    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) as cnt FROM categories WHERE parent_id = %s AND user_id = %lld",
        id_str, (long long)user_id);
    csilk_json_t* cnt_result = csilk_db_query_json(pool, sql);
    if (cnt_result && csilk_json_array_size(cnt_result) > 0) {
        int cnt = (int)db_get_num(csilk_json_array_get(cnt_result, 0), "cnt");
        csilk_json_free(cnt_result);
        if (cnt > 0) { respond_forbidden(c, "分类下有子分类，无法删除"); return; }
    } else {
        if (cnt_result) csilk_json_free(cnt_result);
    }

    snprintf(sql, sizeof(sql), "DELETE FROM categories WHERE id=%s AND user_id=%lld",
             id_str, (long long)user_id);
    csilk_db_exec(pool, sql);
    respond_ok_null(c);
}
