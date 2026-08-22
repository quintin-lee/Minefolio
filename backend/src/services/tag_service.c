#include "services/tag_service.h"
#include "repositories/tag_repo.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <string.h>

void tags_list(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    const char* id_str = csilk_get_param(c, "id");
    if (id_str) {
        csilk_db_pool_t* pool = db_get_pool();
        char idbuf[32], uidbuf[32];
        snprintf(idbuf, sizeof(idbuf), "%lld", (long long)atoll(id_str));
        snprintf(uidbuf, sizeof(uidbuf), "%lld", (long long)user_id);
        csilk_json_t* res = csilk_db_query_param_json(pool,
            "SELECT id, name, color, created_at FROM tags WHERE id=? AND user_id=?",
            (const char*[]){ idbuf, uidbuf, NULL });
        if (!res || csilk_json_array_size(res) == 0) {
            if (res) csilk_json_free(res);
            respond_not_found(c);
            return;
        }
        respond_ok(c, res);
        csilk_json_free(res);
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* result = tag_list(pool, user_id);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
    csilk_json_free(result);
}

void tags_create(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    if (!name || name[0] == '\0') {
        csilk_json_free(body);
        respond_bad_request(c, "标签名称不能为空");
        return;
    }
    const char* color = csilk_json_get_string(body, "color");

    csilk_db_pool_t* pool = db_get_pool();
    int64_t id = tag_insert(pool, user_id, name, color);
    csilk_json_free(body);
    if (id <= 0) { respond_error(c, 500, "创建失败"); return; }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", (double)id);
    respond_ok(c, resp);
}

void tags_update(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    const char* color = csilk_json_get_string(body, "color");

    csilk_db_pool_t* pool = db_get_pool();
    int ok = tag_update(pool, user_id, atoll(id_str), name, color);
    csilk_json_free(body);
    if (!ok) { respond_not_found(c); return; }
    respond_ok_null(c);
}

void tags_delete(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    int ok = tag_delete(pool, user_id, atoll(id_str));
    if (!ok) { respond_not_found(c); return; }
    respond_ok_null(c);
}

void tags_suggestions(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;

    const char* q = csilk_get_query(c, "q");
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* result = tag_suggestions(pool, user_id, q);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
    csilk_json_free(result);
}

void register_tag_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/tags", tags_list, nullptr, "tag_resp_t", "List tags", "Returns all tags for the current user");
    csilk_app_post_ext(app, "/api/tags", tags_create, "tag_req_t", "tag_resp_t", "Create tag", "Create a new tag");
    csilk_app_put_ext(app, "/api/tags/:id", tags_update, "tag_req_t", "tag_resp_t", "Update tag", "Update an existing tag by ID");
    csilk_app_delete_ext(app, "/api/tags/:id", tags_delete, nullptr, nullptr, "Delete tag", "Delete a tag by ID");
    csilk_app_get_ext(app, "/api/tags/suggestions", tags_suggestions, nullptr, "tag_resp_t", "Tag suggestions", "Returns tag suggestions for autocomplete");
}
