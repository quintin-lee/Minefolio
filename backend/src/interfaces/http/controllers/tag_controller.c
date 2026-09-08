#include "interfaces/http/controllers/tag_controller.h"
#include "application/tag/usecases.h"
#include "application/tag/commands.h"
#include "domain/tag/entity.h"
#include "infrastructure/repositories/tag_repo_impl.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"

void
api_tags_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    /* Single tag by ID */
    const char* id_str = csilk_get_param(c, "id");
    if (id_str) {
        mf_tag_t tag = {0};
        int      rc = mf_tag_repo_find_by_id(db_get_pool(), user_id, atoll(id_str), &tag);
        if (rc == 1) {
            respond_not_found(c);
            return;
        }
        if (rc != 0) {
            respond_error(c, 500, "查询失败");
            return;
        }
        csilk_json_t* obj = csilk_json_object();
        csilk_json_add_number(obj, "id", (double)tag.id);
        csilk_json_add_string(obj, "name", tag.name);
        csilk_json_add_string(obj, "color", tag.color);
        csilk_json_add_string(obj, "created_at", tag.created_at);
        respond_ok(c, obj);
        return;
    }

    /* List all tags */
    csilk_json_t* list = NULL;
    if (tag_usecase_list(db_get_pool(), user_id, &list) != 0) {
        respond_error(c, 500, "查询标签列表失败");
        return;
    }
    respond_ok(c, list ? list : csilk_json_array());
}

void
api_tags_create(csilk_ctx_t* c)
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

    create_tag_cmd_t cmd = {
        .user_id = user_id,
        .name = csilk_json_get_string(body, "name"),
        .color = csilk_json_get_string(body, "color"),
    };

    int64_t              new_id = 0;
    tag_usecase_result_t res = {0};
    int                  rc = tag_usecase_create(db_get_pool(), &cmd, &new_id, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_number(resp, "id", (double)new_id);
        respond_ok(c, resp);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "创建标签失败");
    }
}

void
api_tags_update(csilk_ctx_t* c)
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

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    update_tag_cmd_t cmd = {
        .user_id = user_id,
        .tag_id = atoll(id_str),
        .name = csilk_json_get_string(body, "name"),
        .color = csilk_json_get_string(body, "color"),
    };

    tag_usecase_result_t res = {0};
    int                  rc = tag_usecase_update(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "更新标签失败");
    }
}

void
api_tags_delete(csilk_ctx_t* c)
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

    delete_tag_cmd_t cmd = {
        .user_id = user_id,
        .tag_id = atoll(id_str),
    };

    tag_usecase_result_t res = {0};
    int                  rc = tag_usecase_delete(db_get_pool(), &cmd, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message[0] ? res.message : "删除标签失败");
    }
}

void
api_tags_suggestions(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char*   q = csilk_get_query(c, "q");
    csilk_json_t* list = NULL;
    if (tag_usecase_suggestions(db_get_pool(), user_id, q, &list) != 0) {
        respond_error(c, 500, "查询标签建议失败");
        return;
    }
    respond_ok(c, list ? list : csilk_json_array());
}

/* Backward-compatibility aliases */
void
tags_list(csilk_ctx_t* c)
{
    api_tags_list(c);
}
void
tags_create(csilk_ctx_t* c)
{
    api_tags_create(c);
}
void
tags_update(csilk_ctx_t* c)
{
    api_tags_update(c);
}
void
tags_delete(csilk_ctx_t* c)
{
    api_tags_delete(c);
}
void
tags_suggestions(csilk_ctx_t* c)
{
    api_tags_suggestions(c);
}

void
register_tag_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/tags",
                      api_tags_list,
                      nullptr,
                      "tag_resp_t",
                      "List tags",
                      "Returns all tags for the current user");
    csilk_app_post_ext(app,
                       "/api/tags",
                       api_tags_create,
                       "tag_req_t",
                       "tag_resp_t",
                       "Create tag",
                       "Create a new tag");
    csilk_app_put_ext(app,
                      "/api/tags/:id",
                      api_tags_update,
                      "tag_req_t",
                      "tag_resp_t",
                      "Update tag",
                      "Update an existing tag by ID");
    csilk_app_delete_ext(app,
                         "/api/tags/:id",
                         api_tags_delete,
                         nullptr,
                         nullptr,
                         "Delete tag",
                         "Delete a tag by ID");
    csilk_app_get_ext(app,
                      "/api/tags/suggestions",
                      api_tags_suggestions,
                      nullptr,
                      "tag_resp_t",
                      "Tag suggestions",
                      "Returns tag suggestions for autocomplete");
}
