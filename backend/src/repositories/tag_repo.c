#include "repositories/tag_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

csilk_json_t* tag_list(csilk_db_pool_t* pool, int64_t user_id) {
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(pool,
        "SELECT id, name, color, created_at FROM tags WHERE user_id=? ORDER BY name",
        (const char*[]){ uid, NULL });
}

csilk_json_t* tag_suggestions(csilk_db_pool_t* pool, int64_t user_id, const char* prefix) {
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    if (!prefix || prefix[0] == '\0') {
        return csilk_db_query_param_json(pool,
            "SELECT id, name, color FROM tags WHERE user_id=? LIMIT 20",
            (const char*[]){ uid, NULL });
    }
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%%%s%%", prefix);
    return csilk_db_query_param_json(pool,
        "SELECT id, name, color FROM tags WHERE user_id=? AND name LIKE ? LIMIT 10",
        (const char*[]){ uid, pattern, NULL });
}

int64_t tag_insert(csilk_db_pool_t* pool, int64_t user_id, const char* name, const char* color) {
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    const char* col = color && color[0] ? color : "#666666";
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "INSERT INTO tags (user_id, name, color) VALUES (?, ?, ?) RETURNING id",
        (const char*[]){ uid, name, col, NULL });
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0)
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    if (res) csilk_json_free(res);
    return id;
}

int tag_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* name, const char* color) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "UPDATE tags SET name=?, color=? WHERE id=? AND user_id=?",
        (const char*[]){ name ? name : "", color ? color : "", idstr, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}

int tag_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "DELETE FROM tags WHERE id=? AND user_id=?",
        (const char*[]){ idstr, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}
