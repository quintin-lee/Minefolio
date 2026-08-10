#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void tags_list(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT id, name, color, created_at FROM tags WHERE user_id=%lld ORDER BY name",
        (long long)user_id);

    csilk_json_t* result = csilk_db_query_json(pool, sql);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
}

static void tags_create(csilk_ctx_t* c) {
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
    char sql[256];
    snprintf(sql, sizeof(sql),
        "INSERT INTO tags (user_id, name, color) VALUES (%lld, '%s', '%s')",
        (long long)user_id, name, color);

    if (csilk_db_exec(pool, sql) != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_free(body);
    respond_ok_null(c);
}

static void tags_update(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* name = csilk_json_get_string(body, "name");
    const char* color = csilk_json_get_string(body, "color");

    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE tags SET name='%s', color='%s' WHERE id=%s AND user_id=%lld",
        name ? name : "", color ? color : "", id_str, (long long)user_id);

    csilk_db_exec(db_get_pool(), sql);
    csilk_json_free(body);
    respond_ok_null(c);
}

static void tags_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char sql[256];
    snprintf(sql, sizeof(sql),
        "DELETE FROM tags WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_db_exec(pool, sql);
    respond_ok_null(c);
}

static void tags_suggestions(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* q = csilk_get_query(c, "q");
    csilk_db_pool_t* pool = db_get_pool();
    char sql[256];
    if (q && strlen(q) > 0) {
        snprintf(sql, sizeof(sql),
            "SELECT id, name, color FROM tags WHERE user_id=%lld AND name LIKE '%%%s%%' LIMIT 10",
            (long long)user_id, q);
    } else {
        snprintf(sql, sizeof(sql),
            "SELECT id, name, color FROM tags WHERE user_id=%lld LIMIT 20",
            (long long)user_id);
    }

    csilk_json_t* result = csilk_db_query_json(pool, sql);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result);
}
