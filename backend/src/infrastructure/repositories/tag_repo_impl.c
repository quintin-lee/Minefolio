#include "infrastructure/repositories/tag_repo_impl.h"
#include "common/db.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int
mf_tag_repo_list(void* db_pool, int64_t user_id, mf_tag_t** out_list, size_t* out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* rows =
        csilk_db_query_param_json(pool,
                                  "SELECT id, user_id, name, color, created_at, updated_at "
                                  "FROM tags WHERE user_id=? ORDER BY name",
                                  (const char*[]){uid_str, NULL});

    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return 0;
    }

    size_t    n = csilk_json_array_size(rows);
    mf_tag_t* list = n > 0 ? (mf_tag_t*)malloc(sizeof(mf_tag_t) * n) : NULL;
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        list[i].id = db_get_int(r, "id");
        list[i].user_id = db_get_int(r, "user_id");
        strncpy(list[i].name,
                csilk_json_get_string(r, "name") ? csilk_json_get_string(r, "name") : "",
                sizeof(list[i].name) - 1);
        strncpy(list[i].color,
                csilk_json_get_string(r, "color") ? csilk_json_get_string(r, "color") : "",
                sizeof(list[i].color) - 1);
        strncpy(list[i].created_at,
                csilk_json_get_string(r, "created_at") ? csilk_json_get_string(r, "created_at")
                                                       : "",
                sizeof(list[i].created_at) - 1);
        strncpy(list[i].updated_at,
                csilk_json_get_string(r, "updated_at") ? csilk_json_get_string(r, "updated_at")
                                                       : "",
                sizeof(list[i].updated_at) - 1);
    }
    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

int
mf_tag_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_tag_t* out)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    csilk_json_t* rows =
        csilk_db_query_param_json(pool,
                                  "SELECT id, user_id, name, color, created_at, updated_at "
                                  "FROM tags WHERE id=? AND user_id=?",
                                  (const char*[]){id_str, uid_str, NULL});

    if (!rows || csilk_json_array_size(rows) == 0) {
        if (rows) {
            csilk_json_free(rows);
        }
        return 1; /* not found */
    }

    csilk_json_t* r = csilk_json_array_get(rows, 0);
    out->id = db_get_int(r, "id");
    out->user_id = db_get_int(r, "user_id");
    strncpy(out->name,
            csilk_json_get_string(r, "name") ? csilk_json_get_string(r, "name") : "",
            sizeof(out->name) - 1);
    strncpy(out->color,
            csilk_json_get_string(r, "color") ? csilk_json_get_string(r, "color") : "",
            sizeof(out->color) - 1);
    strncpy(out->created_at,
            csilk_json_get_string(r, "created_at") ? csilk_json_get_string(r, "created_at") : "",
            sizeof(out->created_at) - 1);
    strncpy(out->updated_at,
            csilk_json_get_string(r, "updated_at") ? csilk_json_get_string(r, "updated_at") : "",
            sizeof(out->updated_at) - 1);
    csilk_json_free(rows);
    return 0;
}

int
mf_tag_repo_suggestions(
    void* db_pool, int64_t user_id, const char* prefix, mf_tag_t** out_list, size_t* out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* rows;
    if (!prefix || prefix[0] == '\0') {
        rows =
            csilk_db_query_param_json(pool,
                                      "SELECT id, name, color FROM tags WHERE user_id=? LIMIT 20",
                                      (const char*[]){uid_str, NULL});
    } else {
        char pattern[256];
        snprintf(pattern, sizeof(pattern), "%%%s%%", prefix);
        rows = csilk_db_query_param_json(
            pool,
            "SELECT id, name, color FROM tags WHERE user_id=? AND name LIKE ? LIMIT 10",
            (const char*[]){uid_str, pattern, NULL});
    }

    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return 0;
    }

    size_t    n = csilk_json_array_size(rows);
    mf_tag_t* list = n > 0 ? (mf_tag_t*)malloc(sizeof(mf_tag_t) * n) : NULL;
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        list[i].id = db_get_int(r, "id");
        strncpy(list[i].name,
                csilk_json_get_string(r, "name") ? csilk_json_get_string(r, "name") : "",
                sizeof(list[i].name) - 1);
        strncpy(list[i].color,
                csilk_json_get_string(r, "color") ? csilk_json_get_string(r, "color") : "",
                sizeof(list[i].color) - 1);
    }
    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

int
mf_tag_repo_create(
    void* db_pool, int64_t user_id, const char* name, const char* color, int64_t* out_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char*   col = (color && color[0]) ? color : "#666666";
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO tags (user_id, name, color) VALUES (?, ?, ?) RETURNING id",
        (const char*[]){uid_str, name, col, NULL});

    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }
    *out_id = db_get_int(csilk_json_array_get(res, 0), "id");
    csilk_json_free(res);
    return 0;
}

int
mf_tag_repo_update(void* db_pool, int64_t user_id, int64_t id, const char* name, const char* color)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    const char* n = name ? name : "";
    const char* c = (color && color[0]) ? color : "";

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE tags SET name=?, color=? WHERE id=? AND user_id=? RETURNING id",
        (const char*[]){n, c, id_str, uid_str, NULL});

    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : 1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_tag_repo_delete(void* db_pool, int64_t user_id, int64_t id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "DELETE FROM tags WHERE id=? AND user_id=? RETURNING id",
                                  (const char*[]){id_str, uid_str, NULL});

    int ok = (res && csilk_json_array_size(res) > 0) ? 0 : 1;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

void
mf_tag_repo_free_list(mf_tag_t* list, size_t count)
{
    (void)count;
    free(list);
}
