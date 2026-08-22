#include "repositories/auth_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

csilk_json_t* user_find_by_username(csilk_db_pool_t* pool, const char* username) {
    return csilk_db_query_param_json(pool,
        "SELECT id, username, created_at FROM users WHERE username = ?",
        (const char*[]){ username, NULL });
}

int64_t user_insert(csilk_db_pool_t* pool, const char* username, const char* password_hash) {
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "INSERT INTO users (username, password) VALUES (?, ?) RETURNING id",
        (const char*[]){ username, password_hash, NULL });
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0)
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    if (res) csilk_json_free(res);
    return id;
}

csilk_json_t* user_get_by_id(csilk_db_pool_t* pool, int64_t user_id) {
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(pool,
        "SELECT id, username, created_at FROM users WHERE id = ?",
        (const char*[]){ uid, NULL });
}

int user_update_password(csilk_db_pool_t* pool, int64_t user_id, const char* password_hash) {
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "UPDATE users SET password = ? WHERE id = ?",
        (const char*[]){ password_hash, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}

int user_update_token_version(csilk_db_pool_t* pool, int64_t user_id) {
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "UPDATE users SET token_version = token_version + 1 WHERE id = ?",
        (const char*[]){ uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}

int user_count(csilk_db_pool_t* pool) {
    csilk_json_t* res = csilk_db_query_param_json(pool, "SELECT COUNT(*) as count FROM users", (const char*[]){NULL});
    int count = 0;
    if (res && csilk_json_array_size(res) > 0)
        count = (int)db_get_int(csilk_json_array_get(res, 0), "count");
    if (res) csilk_json_free(res);
    return count;
}

int system_is_initialized(csilk_db_pool_t* pool) {
    return user_count(pool) > 0;
}
