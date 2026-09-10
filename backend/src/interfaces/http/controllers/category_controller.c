#include "interfaces/http/controllers/category_controller.h"
#include "application/category/usecases.h"
#include "application/category/commands.h"
#include "domain/category/entity.h"
#include "infrastructure/repositories/category_repo_impl.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
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
        double              id_val = db_get_num(row, "id");
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
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }

    category_usecase_seed_defaults(db_get_pool(), user_id, ledger_id);
    const char* type_query = csilk_get_query(c, "type");

    if (type_query && strstr(type_query, ",")) {
        csilk_json_t* merged = csilk_json_array();
        char          copy[128];
        strncpy(copy, type_query, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        char* tok = strtok(copy, ",");
        while (tok) {
            while (*tok == ' ') {
                tok++;
            }
            if (tok[0]) {
                csilk_json_t* part = NULL;
                if (category_usecase_list(db_get_pool(), user_id, ledger_id, tok, &part) == 0 &&
                    part) {
                    size_t pn = csilk_json_array_size(part);
                    for (size_t i = 0; i < pn; i++) {
                        csilk_json_add_item(merged, csilk_json_array_get(part, i));
                    }
                    csilk_json_free(part);
                }
            }
            tok = strtok(NULL, ",");
        }
        csilk_json_t* tree = build_tree(merged);
        csilk_json_free(merged);
        respond_ok(c, tree);
        csilk_json_free(tree);
        return;
    }

    csilk_json_t* list = NULL;
    if (category_usecase_list(db_get_pool(), user_id, ledger_id, type_query, &list) != 0) {
        respond_error(c, 500, "查询分类列表失败");
        return;
    }
    csilk_json_t* empty = csilk_json_array();
    csilk_json_t* tree = build_tree(list ? list : empty);
    if (!list) {
        csilk_json_free(empty);
    } else {
        csilk_json_free(list);
    }
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
    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }
    create_category_cmd_t cmd = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .name = csilk_json_get_string(body, "name"),
        .parent_id = (int64_t)db_get_num(body, "parent_id"),
        .type = csilk_json_get_string(body, "type"),
        .asset_type = csilk_json_get_string(body, "asset_type"),
        .currency = csilk_json_get_string(body, "currency"),
        .icon = csilk_json_get_string(body, "icon"),
        .sort_order = (int)db_get_num(body, "sort_order"),
    };

    int64_t                   new_id = 0;
    category_usecase_result_t res = {0};
    int                       rc = category_usecase_create(db_get_pool(), &cmd, &new_id, &res);
    csilk_json_free(body);
    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "创建分类失败");
    }
}

void
api_categories_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }
    update_category_cmd_t cmd = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .category_id = atoll(id_str),
        .name = csilk_json_get_string(body, "name"),
        .type = csilk_json_get_string(body, "type"),
        .asset_type = csilk_json_get_string(body, "asset_type"),
        .currency = csilk_json_get_string(body, "currency"),
        .icon = csilk_json_get_string(body, "icon"),
        .sort_order = (int)db_get_num(body, "sort_order"),
    };
    category_usecase_result_t res = {0};
    int                       rc = category_usecase_update(db_get_pool(), &cmd, &res);
    csilk_json_free(body);
    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "更新分类失败");
    }
}

void
api_categories_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }
    delete_category_cmd_t cmd = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .category_id = atoll(id_str),
    };
    category_usecase_result_t res = {0};
    int                       rc = category_usecase_delete(db_get_pool(), &cmd, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else if (res.code == 1004) {
        respond_forbidden(c, res.message);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "删除分类失败");
    }
}

void
api_categories_children(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }
    int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");
    if (ledger_id < 0) {
        return;
    }
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少 id");
        return;
    }

    category_usecase_seed_defaults(db_get_pool(), user_id, ledger_id);
    csilk_json_t* list = NULL;
    if (category_usecase_children(db_get_pool(), user_id, ledger_id, atoll(id_str), &list) != 0) {
        respond_error(c, 500, "查询子分类失败");
        return;
    }
    respond_ok(c, list ? list : csilk_json_array());
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
                      "Returns categories owned by the current ledger");
    csilk_app_post_ext(app,
                       "/api/categories",
                       api_categories_create,
                       "category_req_t",
                       "category_resp_t",
                       "Create category",
                       "Create a category");
    csilk_app_put_ext(app,
                      "/api/categories/:id",
                      api_categories_update,
                      "category_req_t",
                      "category_resp_t",
                      "Update category",
                      "Update a category");
    csilk_app_delete_ext(app,
                         "/api/categories/:id",
                         api_categories_delete,
                         nullptr,
                         nullptr,
                         "Delete category",
                         "Delete a category");
    csilk_app_get_ext(app,
                      "/api/categories/:id/children",
                      api_categories_children,
                      nullptr,
                      "category_resp_t",
                      "List category children",
                      "List child categories");
}
