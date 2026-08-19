#include "services/tag_service.h"
#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tags_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, NULL };

    csilk_json_t* result = csilk_db_query_param_json(pool,
        "SELECT id, name, color, created_at FROM tags WHERE user_id=? ORDER BY name", params);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
}

void tags_create(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    if (!name || strlen(name) == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "标签名称不能为空");
        return;
    }

    const char* color = csilk_json_get_string(body, "color");
    if (!color) color = "#666666";

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, name, color, NULL };

    csilk_json_t* res = csilk_db_query_param_json(pool,
        "INSERT INTO tags (user_id, name, color) VALUES (?, ?, ?)", params);
    if (!res) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_free(res);
    csilk_json_free(body);
    respond_ok_null(c);
}

void tags_update(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    const char* color = csilk_json_get_string(body, "color");

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { name ? name : "", color ? color : "", id_str, uid_str, NULL };

    csilk_json_t* res = csilk_db_query_param_json(db_get_pool(),
        "UPDATE tags SET name=?, color=? WHERE id=? AND user_id=?", params);
    if (res) csilk_json_free(res);

    csilk_json_free(body);
    respond_ok_null(c);
}

void tags_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { id_str, uid_str, NULL };

    csilk_json_t* res = csilk_db_query_param_json(pool,
        "DELETE FROM tags WHERE id=? AND user_id=?", params);
    if (res) csilk_json_free(res);

    respond_ok_null(c);
}

void tags_suggestions(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* q = csilk_get_query(c, "q");
    csilk_db_pool_t* pool = db_get_pool();
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* result = NULL;
    if (q && strlen(q) > 0) {
        char pattern[256];
        snprintf(pattern, sizeof(pattern), "%%%s%%", q);
        const char* params[] = { uid_str, pattern, NULL };
        result = csilk_db_query_param_json(pool,
            "SELECT id, name, color FROM tags WHERE user_id=? AND name LIKE ? LIMIT 10", params);
    } else {
        const char* params[] = { uid_str, NULL };
        result = csilk_db_query_param_json(pool,
            "SELECT id, name, color FROM tags WHERE user_id=? LIMIT 20", params);
    }

    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
}
