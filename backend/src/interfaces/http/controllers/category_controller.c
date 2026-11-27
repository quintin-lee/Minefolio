#include "interfaces/http/controllers/category_controller.h"
#include "application/category/usecases.h"
#include "application/account/usecases.h"
#include "application/category/commands.h"
#include "domain/category/entity.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "yyjson.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline size_t
ht_hash_id(int64_t id, size_t mask)
{
    return (size_t)(((uint64_t)id * 11400714819323198485ULL) & (uint64_t)mask);
}

#include <ctype.h>

static bool
cat_contains_icase(const char* haystack, const char* needle)
{
    if (!needle || !needle[0]) {
        return true;
    }
    if (!haystack || !haystack[0]) {
        return false;
    }
    size_t nlen = strlen(needle);
    for (const char* p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) {
            return true;
        }
    }
    return false;
}

static void
cat_mark_all_descendants(size_t        idx,
                         const size_t* first_child,
                         const size_t* next_sibling,
                         size_t        n,
                         uint8_t*      keep,
                         int           depth)
{
    if (idx >= n || depth > 64 || keep[idx]) {
        return;
    }
    keep[idx] = 1;
    for (size_t c = first_child[idx]; c < n; c = next_sibling[c]) {
        cat_mark_all_descendants(c, first_child, next_sibling, n, keep, depth + 1);
    }
}

static bool
cat_mark_filter(size_t               idx,
                const mf_category_t* rows,
                const size_t*        first_child,
                const size_t*        next_sibling,
                size_t               n,
                const char*          kw,
                uint8_t*             keep,
                int                  depth)
{
    if (idx >= n || depth > 64) {
        return false;
    }
    if (cat_contains_icase(rows[idx].name, kw)) {
        cat_mark_all_descendants(idx, first_child, next_sibling, n, keep, depth);
        return true;
    }
    bool any = false;
    for (size_t c = first_child[idx]; c < n; c = next_sibling[c]) {
        if (cat_mark_filter(c, rows, first_child, next_sibling, n, kw, keep, depth + 1)) {
            any = true;
        }
    }
    if (any) {
        keep[idx] = 1;
    }
    return any;
}

static yyjson_mut_val*
emit_cat_node_json(yyjson_mut_doc*      doc,
                   size_t               idx,
                   const mf_category_t* rows,
                   const size_t*        first_child,
                   const size_t*        next_sibling,
                   size_t               n,
                   const uint8_t*       keep,
                   int                  depth)
{
    const mf_category_t* r = &rows[idx];
    yyjson_mut_val*      obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, obj, "id", r->id);
    yyjson_mut_obj_add_strcpy(doc, obj, "name", r->name);
    yyjson_mut_obj_add_null(doc, obj, "parent_name");

    if (r->parent_id > 0) {
        yyjson_mut_obj_add_int(doc, obj, "parent_id", r->parent_id);
    } else {
        yyjson_mut_obj_add_null(doc, obj, "parent_id");
    }

    yyjson_mut_obj_add_strcpy(doc, obj, "type", r->type[0] ? r->type : "asset");
    if (r->asset_type[0]) {
        yyjson_mut_obj_add_strcpy(doc, obj, "asset_type", r->asset_type);
    } else {
        yyjson_mut_obj_add_null(doc, obj, "asset_type");
    }
    if (r->currency[0]) {
        yyjson_mut_obj_add_strcpy(doc, obj, "currency", r->currency);
    } else {
        yyjson_mut_obj_add_null(doc, obj, "currency");
    }
    yyjson_mut_obj_add_strcpy(doc, obj, "icon", r->icon);
    yyjson_mut_obj_add_int(doc, obj, "sort_order", r->sort_order);

    if (depth < 64) {
        yyjson_mut_val* ch_arr = NULL;
        for (size_t c = first_child[idx]; c < n; c = next_sibling[c]) {
            if (!keep[c]) {
                continue;
            }
            if (!ch_arr) {
                ch_arr = yyjson_mut_arr(doc);
                yyjson_mut_obj_add_val(doc, obj, "children", ch_arr);
            }
            yyjson_mut_val* child_obj =
                emit_cat_node_json(doc, c, rows, first_child, next_sibling, n, keep, depth + 1);
            if (child_obj) {
                yyjson_mut_arr_add_val(ch_arr, child_obj);
            }
        }
    }
    return obj;
}

static char*
build_tree_envelope_json(const mf_category_t* rows,
                         size_t               n,
                         const char*          keyword,
                         int64_t              page,
                         int64_t              page_size,
                         size_t*              out_len)
{
    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 20;
    }
    if (page_size > 500) {
        page_size = 500;
    }

    if (n == 0 || !rows) {
        char buf[192];
        int  len = snprintf(buf,
                            sizeof(buf),
                            "{\"code\":0,\"message\":\"ok\",\"data\":{\"list\":[],\"total\":0,"
                            "\"page\":%lld,\"page_size\":%lld}}",
                            (long long)page,
                            (long long)page_size);
        char* empty_json = (len > 0) ? strdup(buf) : NULL;
        if (out_len) {
            *out_len = empty_json ? (size_t)len : 0;
        }
        return empty_json;
    }

    size_t ht_cap = 16;
    while (ht_cap < n * 2) {
        ht_cap <<= 1;
    }
    size_t ht_mask = ht_cap - 1;

    int64_t* ht_keys = calloc(ht_cap, sizeof(int64_t));
    size_t*  ht_vals = calloc(ht_cap, sizeof(size_t));
    size_t*  first_child = malloc(n * sizeof(size_t));
    size_t*  last_child = malloc(n * sizeof(size_t));
    size_t*  next_sibling = malloc(n * sizeof(size_t));
    size_t*  root_indices = malloc(n * sizeof(size_t));
    uint8_t* keep = calloc(n, sizeof(uint8_t));
    if (!ht_keys || !ht_vals || !first_child || !last_child || !next_sibling || !root_indices ||
        !keep) {
        free(ht_keys);
        free(ht_vals);
        free(first_child);
        free(last_child);
        free(next_sibling);
        free(root_indices);
        free(keep);
        return NULL;
    }

    for (size_t i = 0; i < n; i++) {
        first_child[i] = n;
        last_child[i] = n;
        next_sibling[i] = n;
        if (rows[i].id > 0) {
            size_t pos = ht_hash_id(rows[i].id, ht_mask);
            while (ht_keys[pos] != 0 && ht_keys[pos] != rows[i].id) {
                pos = (pos + 1) & ht_mask;
            }
            if (ht_keys[pos] == 0) {
                ht_keys[pos] = rows[i].id;
                ht_vals[pos] = i;
            }
        }
    }

    size_t root_count = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t parent = rows[i].parent_id;
        size_t  j = n;
        if (parent > 0) {
            size_t pos = ht_hash_id(parent, ht_mask);
            while (ht_keys[pos] != 0) {
                if (ht_keys[pos] == parent) {
                    j = ht_vals[pos];
                    break;
                }
                pos = (pos + 1) & ht_mask;
            }
        }
        if (parent > 0 && j < n && j != i) {
            if (first_child[j] == n) {
                first_child[j] = i;
                last_child[j] = i;
            } else {
                next_sibling[last_child[j]] = i;
                last_child[j] = i;
            }
        } else {
            root_indices[root_count++] = i;
        }
    }

    size_t filtered_root_count = 0;
    if (keyword && keyword[0]) {
        for (size_t k = 0; k < root_count; k++) {
            size_t r_idx = root_indices[k];
            if (cat_mark_filter(
                    r_idx, rows, first_child, next_sibling, n, keyword, keep, 0)) {
                root_indices[filtered_root_count++] = r_idx;
            }
        }
    } else {
        memset(keep, 1, n);
        filtered_root_count = root_count;
    }

    yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        free(ht_keys);
        free(ht_vals);
        free(first_child);
        free(last_child);
        free(next_sibling);
        free(root_indices);
        free(keep);
        return NULL;
    }

    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "code", 0);
    yyjson_mut_obj_add_str(doc, root, "message", "ok");

    yyjson_mut_val* data_obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "data", data_obj);
    yyjson_mut_val* list_arr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, data_obj, "list", list_arr);
    yyjson_mut_obj_add_int(doc, data_obj, "total", (int64_t)filtered_root_count);
    yyjson_mut_obj_add_int(doc, data_obj, "page", page);
    yyjson_mut_obj_add_int(doc, data_obj, "page_size", page_size);

    int64_t offset = (page - 1) * page_size;
    size_t  start = (offset >= (int64_t)filtered_root_count) ? filtered_root_count : (size_t)offset;
    size_t  end = start + (size_t)page_size;
    if (end > filtered_root_count) {
        end = filtered_root_count;
    }

    for (size_t k = start; k < end; k++) {
        yyjson_mut_val* node_obj = emit_cat_node_json(
            doc, root_indices[k], rows, first_child, next_sibling, n, keep, 0);
        if (node_obj) {
            yyjson_mut_arr_add_val(list_arr, node_obj);
        }
    }

    free(ht_keys);
    free(ht_vals);
    free(first_child);
    free(last_child);
    free(next_sibling);
    free(root_indices);
    free(keep);

    char* json_str = yyjson_mut_write(doc, 0, out_len);
    yyjson_mut_doc_free(doc);
    return json_str;
}

static uint64_t
fnv1a_hash64(const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    uint64_t       h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void
send_categories_tree_response(csilk_ctx_t*         c,
                              const mf_category_t* rows,
                              size_t               count,
                              const char*          keyword,
                              int64_t              page,
                              int64_t              page_size)
{
    size_t len = 0;
    char*  json_str = build_tree_envelope_json(rows, count, keyword, page, page_size, &len);
    if (!json_str) {
        respond_error(c, 500, "序列化分类树失败");
        return;
    }

    uint64_t h = fnv1a_hash64(json_str, len);
    char     etag[48];
    snprintf(etag, sizeof(etag), "W/\"cat-%016llx\"", (unsigned long long)h);
    csilk_set_header(c, "ETag", etag);
    csilk_set_header(c, "Cache-Control", "private, no-cache");

    const char* inm = csilk_get_header(c, "If-None-Match");
    if (inm && strcmp(inm, etag) == 0) {
        free(json_str);
        csilk_status(c, CSILK_STATUS_NOT_MODIFIED);
        return;
    }

    csilk_status(c, CSILK_STATUS_OK);
    csilk_set_header(c, "Content-Type", "application/json");
    csilk_set_response_body_ex(c, json_str, len, CSILK_OWN_HEAP);
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
    account_usecase_seed_default(db_get_pool(), user_id, ledger_id);
    const char* type_query = csilk_get_query(c, "type");
    const char* keyword = csilk_get_query(c, "keyword");
    int64_t     page = 1;
    int64_t     page_size = 20;
    parse_page_params(c, &page, &page_size);

    if (type_query && strstr(type_query, ",")) {
        mf_category_t* merged = NULL;
        size_t         merged_n = 0;
        size_t         merged_cap = 0;
        char           copy[128];
        char*          saveptr = NULL;
        strncpy(copy, type_query, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        char* tok = strtok_r(copy, ",", &saveptr);
        while (tok) {
            while (*tok == ' ') {
                tok++;
            }
            if (tok[0]) {
                mf_category_t* part = NULL;
                size_t         pn = 0;
                if (category_usecase_list_entities(
                        db_get_pool(), user_id, ledger_id, tok, &part, &pn) == 0 &&
                    part && pn > 0) {
                    if (merged_n + pn > merged_cap) {
                        size_t new_cap = merged_cap ? merged_cap : 64;
                        while (new_cap < merged_n + pn) {
                            new_cap *= 2;
                        }
                        mf_category_t* next = realloc(merged, sizeof(mf_category_t) * new_cap);
                        if (!next) {
                            category_usecase_free_list(part, pn);
                            free(merged);
                            respond_error(c, 500, "查询分类列表失败");
                            return;
                        }
                        merged = next;
                        merged_cap = new_cap;
                    }
                    memcpy(merged + merged_n, part, sizeof(mf_category_t) * pn);
                    merged_n += pn;
                }
                category_usecase_free_list(part, pn);
            }
            tok = strtok_r(NULL, ",", &saveptr);
        }
        send_categories_tree_response(c, merged, merged_n, keyword, page, page_size);
        free(merged);
        return;
    }

    mf_category_t* list = NULL;
    size_t         count = 0;
    if (category_usecase_list_entities(
            db_get_pool(), user_id, ledger_id, type_query, &list, &count) != 0) {
        respond_error(c, 500, "查询分类列表失败");
        return;
    }
    send_categories_tree_response(c, list, count, keyword, page, page_size);
    category_usecase_free_list(list, count);
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
    int64_t     cid = parse_query_int_strict(id_str, -1);
    if (cid <= 0) {
        respond_bad_request(c, "缺少或无效的 id");
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
        .category_id = cid,
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
    int64_t     cid = parse_query_int_strict(id_str, -1);
    if (cid <= 0) {
        respond_bad_request(c, "缺少或无效的 id");
        return;
    }
    delete_category_cmd_t cmd = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .category_id = cid,
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
    int64_t     cid = parse_query_int_strict(id_str, -1);
    if (cid <= 0) {
        respond_bad_request(c, "缺少或无效的 id");
        return;
    }

    category_usecase_seed_defaults(db_get_pool(), user_id, ledger_id);
    account_usecase_seed_default(db_get_pool(), user_id, ledger_id);
    csilk_json_t* list = NULL;
    if (category_usecase_children(db_get_pool(), user_id, ledger_id, cid, &list) != 0) {
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
