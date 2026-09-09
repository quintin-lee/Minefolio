#include "infrastructure/repositories/auth_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- SQL statements inlined from repositories/auth_repo.c --- */

int
mf_auth_repo_find_by_username(void* pool, const char* username, mf_user_t* out_user)
{
    if (!pool || !username || !username[0] || !out_user) {
        return -1;
    }
    memset(out_user, 0, sizeof(*out_user));
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT id, username, password, token_version, totp_secret, totp_enabled, "
        "totp_backup_codes, created_at FROM users WHERE username = ?",
        (const char*[]){username, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1;
    }
    csilk_json_t* row = csilk_json_array_get(res, 0);
    out_user->id = (int64_t)db_get_int(row, "id");
    const char* s = csilk_json_get_string(row, "username");
    if (s) {
        snprintf(out_user->username, sizeof(out_user->username), "%s", s);
    }
    s = csilk_json_get_string(row, "password");
    if (s) {
        snprintf(out_user->password_hash, sizeof(out_user->password_hash), "%s", s);
    }
    out_user->token_version = (int64_t)db_get_int(row, "token_version");
    out_user->totp_enabled = (db_get_int(row, "totp_enabled") != 0);
    s = csilk_json_get_string(row, "totp_secret");
    if (s) {
        snprintf(out_user->totp_secret, sizeof(out_user->totp_secret), "%s", s);
    }
    s = csilk_json_get_string(row, "created_at");
    if (s) {
        snprintf(out_user->created_at, sizeof(out_user->created_at), "%s", s);
    }
    csilk_json_free(res);
    return 0;
}

int
mf_auth_repo_get_by_id(void* pool, int64_t user_id, mf_user_t* out_user)
{
    if (!pool || user_id <= 0 || !out_user) {
        return -1;
    }
    memset(out_user, 0, sizeof(*out_user));
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT id, username, password, token_version, totp_secret, totp_enabled, "
        "totp_backup_codes, created_at FROM users WHERE id = ?",
        (const char*[]){uid, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1;
    }
    csilk_json_t* row = csilk_json_array_get(res, 0);
    out_user->id = (int64_t)db_get_int(row, "id");
    const char* s = csilk_json_get_string(row, "username");
    if (s) {
        snprintf(out_user->username, sizeof(out_user->username), "%s", s);
    }
    s = csilk_json_get_string(row, "password");
    if (!s) {
        s = csilk_json_get_string(row, "password_hash");
    }
    if (s) {
        snprintf(out_user->password_hash, sizeof(out_user->password_hash), "%s", s);
    }
    out_user->token_version = (int64_t)db_get_int(row, "token_version");
    out_user->totp_enabled = (db_get_int(row, "totp_enabled") != 0);
    s = csilk_json_get_string(row, "totp_secret");
    if (s) {
        snprintf(out_user->totp_secret, sizeof(out_user->totp_secret), "%s", s);
    }
    s = csilk_json_get_string(row, "totp_backup_codes");
    if (s) {
        snprintf(out_user->totp_backup_codes, sizeof(out_user->totp_backup_codes), "%s", s);
    }
    s = csilk_json_get_string(row, "created_at");
    if (s) {
        snprintf(out_user->created_at, sizeof(out_user->created_at), "%s", s);
    }
    csilk_json_free(res);
    return 0;
}

int
mf_auth_repo_create(void* pool, const char* username, const char* password_hash, int64_t* out_id)
{
    if (!pool || !username || !password_hash) {
        return -1;
    }
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "INSERT INTO users (username, password) VALUES (?, ?) RETURNING id",
        (const char*[]){username, password_hash, NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    if (id <= 0) {
        return -1;
    }
    if (out_id) {
        *out_id = id;
    }
    return 0;
}

int
mf_auth_repo_update_password(void* pool, int64_t user_id, const char* password_hash)
{
    if (!pool || user_id <= 0 || !password_hash) {
        return -1;
    }
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                                  "UPDATE users SET password = ? WHERE id = ?",
                                                  (const char*[]){password_hash, uid, NULL});
    int           ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_auth_repo_update_token_version(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res =
        csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                  "UPDATE users SET token_version = token_version + 1 WHERE id = ?",
                                  (const char*[]){uid, NULL});
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}

int
mf_auth_repo_set_totp_secret(void* pool, int64_t user_id, const char* secret)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                                  "UPDATE users SET totp_secret = ? WHERE id = ?",
                                                  (const char*[]){secret, uid, NULL});
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

int
mf_auth_repo_enable_totp(void* pool, int64_t user_id, const char* backup_codes_json)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "UPDATE users SET totp_enabled = TRUE, totp_backup_codes = ? WHERE id = ?",
        (const char*[]){backup_codes_json, uid, NULL});
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

int
mf_auth_repo_disable_totp(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "UPDATE users SET totp_secret = '', totp_enabled = FALSE, totp_backup_codes = '' "
        "WHERE id = ?",
        (const char*[]){uid, NULL});
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

int
mf_auth_repo_update_backup_codes(void* pool, int64_t user_id, const char* backup_codes_json)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res =
        csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                  "UPDATE users SET totp_backup_codes = ? WHERE id = ?",
                                  (const char*[]){backup_codes_json, uid, NULL});
    if (res) {
        csilk_json_free(res);
    }
    return 0;
}

int
mf_auth_repo_count(void* pool)
{
    if (!pool) {
        return 0;
    }
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool, "SELECT COUNT(*) as count FROM users", (const char*[]){NULL});
    int count = 0;
    if (res && csilk_json_array_size(res) > 0) {
        count = (int)db_get_int(csilk_json_array_get(res, 0), "count");
    }
    if (res) {
        csilk_json_free(res);
    }
    return count;
}

int
mf_auth_repo_is_initialized(void* pool)
{
    if (!pool) {
        return 0;
    }
    return mf_auth_repo_count(pool) > 0;
}

int
mf_auth_repo_find_by_oauth(void*       pool,
                           const char* provider,
                           const char* oauth_id,
                           mf_user_t*  out_user)
{
    if (!pool || !provider || !provider[0] || !oauth_id || !oauth_id[0] || !out_user) {
        return -1;
    }
    memset(out_user, 0, sizeof(*out_user));
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT id, username, token_version FROM users WHERE oauth_provider = ? AND oauth_id = ?",
        (const char*[]){provider, oauth_id, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1;
    }
    const csilk_json_t* row = csilk_json_array_get(res, 0);
    out_user->id = db_get_int(row, "id");
    const char* username = csilk_json_get_string(row, "username");
    if (username) {
        snprintf(out_user->username, sizeof(out_user->username), "%s", username);
    }
    out_user->token_version = db_get_int(row, "token_version");
    csilk_json_free(res);
    return 0;
}

int
mf_auth_repo_create_oauth(
    void* pool, const char* username, const char* provider, const char* oauth_id, int64_t* out_id)
{
    if (!pool || !username || !username[0] || !provider || !provider[0] || !oauth_id ||
        !oauth_id[0] || !out_id) {
        return -1;
    }
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "INSERT INTO users (username, password, token_version, oauth_provider, oauth_id) "
        "VALUES (?, '', 0, ?, ?) RETURNING id",
        (const char*[]){username, provider, oauth_id, NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    if (id <= 0) {
        *out_id = -1;
        return -1;
    }
    *out_id = id;
    return 0;
}
