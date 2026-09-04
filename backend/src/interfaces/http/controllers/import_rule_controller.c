#include "interfaces/http/controllers/import_rule_controller.h"
#include "repositories/import_rule_repo.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
import_rule_service_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    rules = import_rule_list(pool, user_id);
    respond_ok(c, rules ? rules : csilk_json_array());
}

void
import_rule_service_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少规则 ID");
        return;
    }

    int64_t          id = atoll(id_str);
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    res = import_rule_get(pool, user_id, id);

    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        respond_not_found(c);
        return;
    }

    csilk_json_t* rule = csilk_json_array_get(res, 0);
    csilk_json_t* copy = csilk_json_copy(rule);
    csilk_json_free(res);
    respond_ok(c, copy);
}

void
import_rule_service_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char* keyword = csilk_json_get_string(body, "keyword");
    const char* match_field = csilk_json_get_string(body, "match_field");
    const char* match_type = csilk_json_get_string(body, "match_type");
    const char* target_type = csilk_json_get_string(body, "target_type");
    int64_t     category_id = db_get_int(body, "category_id");
    int         priority = (int)db_get_int(body, "priority");
    int is_active = csilk_json_get(body, "is_active") ? csilk_json_get_bool(body, "is_active") : 1;

    if (!keyword || !keyword[0]) {
        csilk_json_free(body);
        respond_bad_request(c, "关键词不能为空");
        return;
    }

    if (priority <= 0) {
        priority = 100;
    }

    csilk_db_pool_t* pool = db_get_pool();
    int64_t          new_id = import_rule_insert(pool,
                                                 user_id,
                                                 keyword,
                                                 match_field ? match_field : "all",
                                                 match_type ? match_type : "contains",
                                                 category_id,
                                                 target_type ? target_type : "expense",
                                                 priority,
                                                 is_active);

    csilk_json_free(body);

    if (new_id <= 0) {
        respond_error(c, 500, "创建规则失败");
        return;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", (double)new_id);
    respond_ok(c, resp);
}

void
import_rule_service_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少规则 ID");
        return;
    }

    int64_t       id = atoll(id_str);
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char* keyword = csilk_json_get_string(body, "keyword");
    const char* match_field = csilk_json_get_string(body, "match_field");
    const char* match_type = csilk_json_get_string(body, "match_type");
    const char* target_type = csilk_json_get_string(body, "target_type");
    int64_t     category_id = db_get_int(body, "category_id");
    int         priority = (int)db_get_int(body, "priority");
    int is_active = csilk_json_get(body, "is_active") ? csilk_json_get_bool(body, "is_active") : 1;

    if (!keyword || !keyword[0]) {
        csilk_json_free(body);
        respond_bad_request(c, "关键词不能为空");
        return;
    }

    if (priority <= 0) {
        priority = 100;
    }

    csilk_db_pool_t* pool = db_get_pool();
    int              ok = import_rule_update(pool,
                                             user_id,
                                             id,
                                             keyword,
                                             match_field ? match_field : "all",
                                             match_type ? match_type : "contains",
                                             category_id,
                                             target_type ? target_type : "expense",
                                             priority,
                                             is_active);

    csilk_json_free(body);

    if (!ok) {
        respond_error(c, 500, "更新规则失败");
        return;
    }

    respond_ok_null(c);
}

void
import_rule_service_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) {
        respond_bad_request(c, "缺少规则 ID");
        return;
    }

    int64_t          id = atoll(id_str);
    csilk_db_pool_t* pool = db_get_pool();
    import_rule_delete(pool, user_id, id);
    respond_ok_null(c);
}

void
import_rule_service_reset_defaults(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    char             uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_db_query_param_json(
        pool, "DELETE FROM import_rules WHERE user_id = ?", (const char*[]){uid, NULL});

    import_rule_seed_defaults(pool, user_id);

    csilk_json_t* rules = import_rule_list(pool, user_id);
    respond_ok(c, rules ? rules : csilk_json_array());
}

void
api_import_rule_list(csilk_ctx_t* c)
{
    import_rule_service_list(c);
}
void
api_import_rule_get(csilk_ctx_t* c)
{
    import_rule_service_get(c);
}
void
api_import_rule_create(csilk_ctx_t* c)
{
    import_rule_service_create(c);
}
void
api_import_rule_update(csilk_ctx_t* c)
{
    import_rule_service_update(c);
}
void
api_import_rule_delete(csilk_ctx_t* c)
{
    import_rule_service_delete(c);
}
void
api_import_rule_reset_defaults(csilk_ctx_t* c)
{
    import_rule_service_reset_defaults(c);
}

void
register_import_rule_routes(csilk_app_t* app)
{
    csilk_app_get(app, "/api/import-rules", api_import_rule_list);
    csilk_app_post(app, "/api/import-rules", api_import_rule_create);
    csilk_app_post(app, "/api/import-rules/reset-defaults", api_import_rule_reset_defaults);
    csilk_app_get(app, "/api/import-rules/:id", api_import_rule_get);
    csilk_app_put(app, "/api/import-rules/:id", api_import_rule_update);
    csilk_app_delete(app, "/api/import-rules/:id", api_import_rule_delete);
}
