#include "interfaces/http/controllers/category_controller.h"
#include "services/category_service.h"
#include "repositories/category_repo.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include "yyjson.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static csilk_json_t*
build_tree(csilk_json_t* rows)
{
    size_t n = csilk_json_array_size(rows);
    if (n == 0) {
        return csilk_json_array();
    }

    yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return csilk_json_array();
    }

    yyjson_mut_val* tree_arr = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, tree_arr);

    yyjson_mut_val** nodes = calloc(n, sizeof(yyjson_mut_val*));
    int64_t*         ids = calloc(n, sizeof(int64_t));
    int64_t*         pids = calloc(n, sizeof(int64_t));
    yyjson_mut_val** children_arrs = calloc(n, sizeof(yyjson_mut_val*));

    if (!nodes || !ids || !pids || !children_arrs) {
        free(nodes);
        free(ids);
        free(pids);
        free(children_arrs);
        yyjson_mut_doc_free(doc);
        return csilk_json_array();
    }

    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* row = csilk_json_array_get(rows, i);
        yyjson_mut_val*     obj = yyjson_mut_obj(doc);

        double id_val = db_get_num(row, "id");
        ids[i] = (int64_t)id_val;
        yyjson_mut_obj_add_real(doc, obj, "id", id_val);

        const char* name = csilk_json_get_string(row, "name");
        yyjson_mut_obj_add_strcpy(doc, obj, "name", name ? name : "");

        const char* parent_name = csilk_json_get_string(row, "parent_name");
        if (parent_name) {
            yyjson_mut_obj_add_strcpy(doc, obj, "parent_name", parent_name);
        } else {
            yyjson_mut_obj_add_null(doc, obj, "parent_name");
        }

        double pid = db_get_num(row, "parent_id");
        pids[i] = (int64_t)pid;
        if (pid > 0) {
            yyjson_mut_obj_add_real(doc, obj, "parent_id", pid);
        } else {
            yyjson_mut_obj_add_null(doc, obj, "parent_id");
        }

        const char* type = csilk_json_get_string(row, "type");
        yyjson_mut_obj_add_strcpy(doc, obj, "type", (type && type[0]) ? type : "asset");

        const char* asset_type = csilk_json_get_string(row, "asset_type");
        if (asset_type) {
            yyjson_mut_obj_add_strcpy(doc, obj, "asset_type", asset_type);
        } else {
            yyjson_mut_obj_add_null(doc, obj, "asset_type");
        }

        const char* currency = csilk_json_get_string(row, "currency");
        if (currency) {
            yyjson_mut_obj_add_strcpy(doc, obj, "currency", currency);
        } else {
            yyjson_mut_obj_add_null(doc, obj, "currency");
        }

        const char* icon = csilk_json_get_string(row, "icon");
        yyjson_mut_obj_add_strcpy(doc, obj, "icon", icon ? icon : "");

        yyjson_mut_obj_add_real(doc, obj, "sort_order", db_get_num(row, "sort_order"));

        nodes[i] = obj;
    }

    for (size_t i = 0; i < n; i++) {
        int64_t parent = pids[i];
        size_t  j = 0;
        while (j < n && ids[j] != parent) {
            j++;
        }
        if (parent > 0 && j < n) {
            if (!children_arrs[j]) {
                children_arrs[j] = yyjson_mut_arr(doc);
                yyjson_mut_obj_add_val(doc, nodes[j], "children", children_arrs[j]);
            }
            yyjson_mut_arr_add_val(children_arrs[j], nodes[i]);
        } else {
            yyjson_mut_arr_add_val(tree_arr, nodes[i]);
        }
    }

    free(nodes);
    free(ids);
    free(pids);
    free(children_arrs);

    size_t len = 0;
    char*  json_str = yyjson_mut_write(doc, 0, &len);
    yyjson_mut_doc_free(doc);

    csilk_json_t* tree = NULL;
    if (json_str) {
        tree = csilk_json_parse_len(json_str, len);
        free(json_str);
    }
    return tree ? tree : csilk_json_array();
}

void
api_categories_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char*      type_query = csilk_get_query(c, "type");
    csilk_db_pool_t* pool = db_get_pool();
    categories_seed_defaults(pool, user_id);

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* rows = NULL;
    if (type_query && strlen(type_query) > 0) {
        if (strstr(type_query, ",")) {
            char copy[128];
            strncpy(copy, type_query, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            const char* params[16];
            params[0] = uid_str;
            int   pidx = 1;
            char  in_placeholders[128] = {0};
            char* tok = strtok(copy, ",");
            while (tok && pidx < 15) {
                while (*tok == ' ') {
                    tok++;
                }
                if (strlen(tok) > 0) {
                    if (pidx > 1) {
                        strncat(in_placeholders,
                                ", ",
                                sizeof(in_placeholders) - strlen(in_placeholders) - 1);
                    }
                    strncat(in_placeholders,
                            "?",
                            sizeof(in_placeholders) - strlen(in_placeholders) - 1);
                    params[pidx++] = tok;
                }
                tok = strtok(NULL, ",");
            }
            params[pidx] = NULL;
            char p_sql[512];
            snprintf(p_sql,
                     sizeof(p_sql),
                     "SELECT c.id, c.name, c.parent_id, "
                     "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
                     "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
                     "FROM categories c WHERE user_id = ? AND c.type IN (%s) ORDER BY c.parent_id, "
                     "c.sort_order",
                     in_placeholders[0] ? in_placeholders : "?");
            rows = csilk_db_query_param_json(pool, p_sql, params);
        } else {
            const char* params[] = {uid_str, type_query, NULL};
            rows = csilk_db_query_param_json(
                pool,
                "SELECT c.id, c.name, c.parent_id, "
                "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
                "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
                "FROM categories c WHERE user_id = ? AND c.type = ? ORDER BY c.parent_id, "
                "c.sort_order",
                params);
        }
    } else {
        const char* params[] = {uid_str, NULL};
        rows = csilk_db_query_param_json(
            pool,
            "SELECT c.id, c.name, c.parent_id, "
            "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
            "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
            "FROM categories c WHERE user_id = ? ORDER BY c.parent_id, c.sort_order",
            params);
    }

    if (!rows) {
        respond_error(c, 500, "查询失败");
        return;
    }

    csilk_json_t* tree = build_tree(rows);
    csilk_json_free(rows);
    respond_ok(c, tree);
    csilk_json_free(tree);
}

void
api_categories_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char* name = csilk_json_get_string(body, "name");
    const char* type = csilk_json_get_string(body, "type");
    if (!type || strlen(type) == 0) {
        type = "asset";
    }

    const char* asset_type = csilk_json_get_string(body, "asset_type");
    if (!asset_type || strlen(asset_type) == 0) {
        asset_type = "cash";
    }

    if (!name) {
        csilk_json_free(body);
        respond_bad_request(c, "name 为必填");
        return;
    }

    const char* currency = csilk_json_get_string(body, "currency");
    if (!currency) {
        currency = "CNY";
    }
    const char* icon = csilk_json_get_string(body, "icon");
    if (!icon) {
        icon = "";
    }
    int64_t parent_id = (int64_t)db_get_num(body, "parent_id");
    int     sort_order = (int)db_get_num(body, "sort_order");

    csilk_db_pool_t* pool = db_get_pool();
    char             uid_str[32], pid_str[32], sort_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(pid_str, sizeof(pid_str), "%lld", (long long)parent_id);
    snprintf(sort_str, sizeof(sort_str), "%d", sort_order);

    csilk_json_t* res = NULL;
    if (parent_id > 0) {
        const char* params[] = {
            uid_str, name, pid_str, type, asset_type, currency, icon, sort_str, NULL};
        res = csilk_db_query_param_json(pool,
                                        "INSERT INTO categories (user_id, name, parent_id, type, "
                                        "asset_type, currency, icon, sort_order) "
                                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                                        params);
    } else {
        const char* params[] = {uid_str, name, type, asset_type, currency, icon, sort_str, NULL};
        res = csilk_db_query_param_json(
            pool,
            "INSERT INTO categories (user_id, name, type, asset_type, currency, icon, sort_order) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            params);
    }

    if (!res) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_free(res);
    csilk_json_free(body);
    respond_ok_null(c);
}

void
api_categories_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char* name = csilk_json_get_string(body, "name");
    const char* type = csilk_json_get_string(body, "type");
    if (!type || strlen(type) == 0) {
        type = "asset";
    }

    const char* asset_type = csilk_json_get_string(body, "asset_type");
    if (!asset_type || strlen(asset_type) == 0) {
        asset_type = "cash";
    }

    const char* currency = csilk_json_get_string(body, "currency");
    const char* icon = csilk_json_get_string(body, "icon");
    int         sort_order = (int)db_get_num(body, "sort_order");

    char uid_str[32], sort_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(sort_str, sizeof(sort_str), "%d", sort_order);

    if (!category_update(pool,
                         user_id,
                         atoll(id_str),
                         name ? name : "",
                         type,
                         asset_type,
                         currency ? currency : "CNY",
                         icon ? icon : "",
                         sort_order)) {
        csilk_json_free(body);
        respond_not_found(c);
        return;
    }

    csilk_json_free(body);
    respond_ok_null(c);
}

void
api_categories_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    // Check if has children
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    char pid[32];
    snprintf(pid, sizeof(pid), "%lld", (long long)atoll(id_str));
    csilk_json_t* cnt_result = category_children(pool, user_id, atoll(id_str));
    if (cnt_result && csilk_json_array_size(cnt_result) > 0) {
        int cnt = (int)db_get_num(csilk_json_array_get(cnt_result, 0), "cnt");
        csilk_json_free(cnt_result);
        if (cnt > 0) {
            respond_forbidden(c, "分类下有子分类，无法删除");
            return;
        }
    } else {
        if (cnt_result) {
            csilk_json_free(cnt_result);
        }
    }
    if (!category_delete(pool, user_id, atoll(id_str))) {
        respond_not_found(c);
        return;
    }
    respond_ok_null(c);
}

void
api_categories_children(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    categories_seed_defaults(pool, user_id);

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char*   params[] = {id_str, uid_str, NULL};
    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT c.id, c.name, c.parent_id, "
        "(SELECT p.name FROM categories p WHERE p.id=c.parent_id) as parent_name, "
        "c.type, c.asset_type, c.currency, c.icon, c.sort_order "
        "FROM categories c WHERE c.parent_id = ? AND c.user_id = ? ORDER BY c.sort_order",
        params);

    if (!rows) {
        respond_error(c, 500, "查询失败");
        return;
    }

    size_t        n = csilk_json_array_size(rows);
    csilk_json_t* result = csilk_json_array();
    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* row = csilk_json_array_get(rows, i);
        csilk_json_t*       node = csilk_json_object();
        csilk_json_add_number(node, "id", db_get_num(row, "id"));
        csilk_json_add_string(node, "name", csilk_json_get_string(row, "name"));
        csilk_json_add_string(node, "parent_name", csilk_json_get_string(row, "parent_name"));
        double pid = db_get_num(row, "parent_id");
        if (pid > 0) {
            csilk_json_add_number(node, "parent_id", pid);
        } else {
            csilk_json_add_null(node, "parent_id");
        }
        const char* type = csilk_json_get_string(row, "type");
        csilk_json_add_string(node, "type", (type && type[0]) ? type : "asset");
        csilk_json_add_string(node, "asset_type", csilk_json_get_string(row, "asset_type"));
        csilk_json_add_string(node, "currency", csilk_json_get_string(row, "currency"));
        csilk_json_add_string(node, "icon", csilk_json_get_string(row, "icon"));
        csilk_json_add_number(node, "sort_order", db_get_num(row, "sort_order"));
        csilk_json_array_append(result, node);
    }
    csilk_json_free(rows);
    respond_ok(c, result);
}

void
categories_list(csilk_ctx_t* c)
{
    api_categories_list(c);
}
void
categories_create(csilk_ctx_t* c)
{
    api_categories_create(c);
}
void
categories_update(csilk_ctx_t* c)
{
    api_categories_update(c);
}
void
categories_delete(csilk_ctx_t* c)
{
    api_categories_delete(c);
}
void
categories_children(csilk_ctx_t* c)
{
    api_categories_children(c);
}

void
register_category_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/categories",
                      api_categories_list,
                      nullptr,
                      "category_resp_t",
                      "List categories",
                      "Returns all categories owned by the current user");
    csilk_app_post_ext(app,
                       "/api/categories",
                       api_categories_create,
                       "category_req_t",
                       "category_resp_t",
                       "Create category",
                       "Create a new expense/income/asset/transaction category");
    csilk_app_put_ext(app,
                      "/api/categories/:id",
                      api_categories_update,
                      "category_req_t",
                      "category_resp_t",
                      "Update category",
                      "Update an existing category by ID");
    csilk_app_delete_ext(app,
                         "/api/categories/:id",
                         api_categories_delete,
                         nullptr,
                         nullptr,
                         "Delete category",
                         "Delete a category and its children by ID");
    csilk_app_get_ext(app,
                      "/api/categories/:id/children",
                      api_categories_children,
                      nullptr,
                      "category_resp_t",
                      "List category children",
                      "Returns immediate child categories of the given category");
}
