#include "interfaces/http/controllers/import_rule_controller.h"
#include "application/import_export/import_rule_usecases.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <string.h>

static csilk_json_t*
import_rule_normalize_row(const csilk_json_t* row)
{
    if (!row || !csilk_json_is_object(row)) {
        return NULL;
    }
    csilk_json_t* out = csilk_json_object();
    if (!out) {
        return NULL;
    }
    size_t n = csilk_json_object_size(row);
    for (size_t i = 0; i < n; i++) {
        const char*         key = csilk_json_object_key(row, i);
        const csilk_json_t* val = csilk_json_object_val(row, i);
        if (!key || !val) {
            continue;
        }
        if (strcmp(key, "id") == 0 || strcmp(key, "user_id") == 0 || strcmp(key, "priority") == 0) {
            csilk_json_add_int(out, key, db_get_int(row, key));
        } else if (strcmp(key, "category_id") == 0) {
            if (csilk_json_is_null(val)) {
                csilk_json_add_null(out, key);
            } else {
                csilk_json_add_int(out, key, db_get_int(row, key));
            }
        } else if (strcmp(key, "is_active") == 0) {
            csilk_json_add_bool(out, key, db_get_bool(row, key) != 0);
        } else {
            csilk_json_t* copy = csilk_json_copy(val);
            if (copy && !csilk_json_add_object(out, key, copy)) {
                csilk_json_free(copy);
            }
        }
    }
    return out;
}

static csilk_json_t*
import_rule_normalize_list(csilk_json_t* list)
{
    csilk_json_t* out = csilk_json_array();
    if (!out) {
        if (list) {
            csilk_json_free(list);
        }
        return NULL;
    }
    if (list && csilk_json_is_array(list)) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* norm = import_rule_normalize_row(csilk_json_array_get(list, i));
            if (norm && !csilk_json_array_append(out, norm)) {
                csilk_json_free(norm);
            }
        }
    }
    if (list) {
        csilk_json_free(list);
    }
    return out;
}

static int
import_rule_parse_is_active(const csilk_json_t* body)
{
    const csilk_json_t* v = csilk_json_get(body, "is_active");
    if (!v || csilk_json_is_null(v)) {
        return 1;
    }
    return db_get_bool(body, "is_active");
}

void
import_rule_service_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* rules =
        import_rule_normalize_list(import_rule_usecase_list(db_get_pool(), user_id));
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
    int64_t     id = parse_query_int_strict(id_str, -1);
    if (id <= 0) {
        respond_bad_request(c, "缺少或无效的规则 ID");
        return;
    }

    csilk_json_t* res = import_rule_usecase_get(db_get_pool(), user_id, id);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        respond_not_found(c);
        return;
    }

    csilk_json_t* rule = csilk_json_array_get(res, 0);
    csilk_json_t* norm = import_rule_normalize_row(rule);
    csilk_json_free(res);
    respond_ok(c, norm ? norm : csilk_json_object());
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
    int         is_active = import_rule_parse_is_active(body);

    if (!keyword || !keyword[0]) {
        csilk_json_free(body);
        respond_bad_request(c, "关键词不能为空");
        return;
    }

    int64_t new_id = import_rule_usecase_create(db_get_pool(),
                                                user_id,
                                                keyword,
                                                match_field,
                                                match_type,
                                                category_id,
                                                target_type,
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
    int64_t     id = parse_query_int_strict(id_str, -1);
    if (id <= 0) {
        respond_bad_request(c, "缺少或无效的规则 ID");
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
    int         is_active = import_rule_parse_is_active(body);

    if (!keyword || !keyword[0]) {
        csilk_json_free(body);
        respond_bad_request(c, "关键词不能为空");
        return;
    }

    int ok = import_rule_usecase_update(db_get_pool(),
                                        user_id,
                                        id,
                                        keyword,
                                        match_field,
                                        match_type,
                                        category_id,
                                        target_type,
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
    int64_t     id = parse_query_int_strict(id_str, -1);
    if (id <= 0) {
        respond_bad_request(c, "缺少或无效的规则 ID");
        return;
    }

    import_rule_usecase_delete(db_get_pool(), user_id, id);
    respond_ok_null(c);
}

void
import_rule_service_reset_defaults(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* rules =
        import_rule_normalize_list(import_rule_usecase_reset_defaults(db_get_pool(), user_id));
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
