#include "repositories/category_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

csilk_json_t* category_list(csilk_db_pool_t* pool, int64_t user_id, const char* type) {
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    if (type && type[0]) {
        return csilk_db_query_param_json(pool, "SELECT c.id,c.name,c.parent_id,c.type,c.asset_type,c.currency,c.icon,c.sort_order,c.created_at,c.updated_at FROM categories c WHERE c.user_id=? AND c.type=? ORDER BY c.sort_order,c.name", (const char*[]){ uid, type, NULL });
    }
    return csilk_db_query_param_json(pool, "SELECT c.id,c.name,c.parent_id,c.type,c.asset_type,c.currency,c.icon,c.sort_order,c.created_at,c.updated_at FROM categories c WHERE c.user_id=? ORDER BY c.sort_order,c.name", (const char*[]){ uid, NULL });
}
csilk_json_t* category_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    return csilk_db_query_param_json(pool, "SELECT id,name,parent_id,type,asset_type,currency,icon,sort_order FROM categories WHERE id=? AND user_id=?", (const char*[]){ idstr, uid, NULL });
}
int64_t category_insert(csilk_db_pool_t* pool, int64_t user_id, const char* name, int64_t parent_id, const char* type, const char* asset_type, const char* currency, const char* icon, int sort_order) {
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)parent_id);
    char sort_str[16]; snprintf(sort_str, sizeof(sort_str), "%d", sort_order);
    csilk_json_t* res = csilk_db_query_param_json(pool, "INSERT INTO categories (user_id,name,parent_id,type,asset_type,currency,icon,sort_order) VALUES (?,?,?,?,?,?,?,?) RETURNING id", (const char*[]){ uid, name, parent_id > 0 ? pid : "NULL", type ? type : "", asset_type ? asset_type : "", currency ? currency : "", icon ? icon : "", sort_str, NULL });
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) id = db_get_int(csilk_json_array_get(res, 0), "id");
    if (res) csilk_json_free(res);
    return id;
}
int category_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* name, const char* type, const char* asset_type, const char* currency, const char* icon, int sort_order) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    char sort_str[16]; snprintf(sort_str, sizeof(sort_str), "%d", sort_order);
    csilk_json_t* res = csilk_db_query_param_json(pool, "UPDATE categories SET name=?,type=?,asset_type=?,currency=?,icon=?,sort_order=? WHERE id=? AND user_id=?", (const char*[]){ name ? name : "", type ? type : "", asset_type ? asset_type : "", currency ? currency : "", icon ? icon : "", sort_str, idstr, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}
int category_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(pool, "DELETE FROM categories WHERE id=? AND user_id=?", (const char*[]){ idstr, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}
csilk_json_t* category_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_id) {
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)parent_id);
    return csilk_db_query_param_json(pool, "SELECT COUNT(*) as cnt FROM categories WHERE parent_id=? AND user_id=?", (const char*[]){ pid, uid, NULL });
}
int64_t category_find_or_create(csilk_db_pool_t* pool, int64_t user_id, const char* name, int64_t parent_id, const char* type, const char* asset_type, const char* icon, int sort_order) {
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)parent_id);
    csilk_json_t* chk = csilk_db_query_param_json(pool, "SELECT id FROM categories WHERE user_id=? AND name=? AND parent_id=?", (const char*[]){ uid, name, parent_id > 0 ? pid : "NULL", NULL });
    if (chk && csilk_json_array_size(chk) > 0) {
        int64_t id = db_get_int(csilk_json_array_get(chk, 0), "id");
        csilk_json_free(chk);
        return id;
    }
    if (chk) csilk_json_free(chk);
    return category_insert(pool, user_id, name, parent_id, type, asset_type, "", icon && icon[0] ? icon : "", sort_order);
}
int category_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(pool, "SELECT id FROM categories WHERE id=? AND user_id=?", (const char*[]){ idstr, uid, NULL });
    int ok = res && csilk_json_array_size(res) > 0;
    if (res) csilk_json_free(res);
    return ok;
}
