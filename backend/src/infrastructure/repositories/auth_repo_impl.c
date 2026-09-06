#include "infrastructure/repositories/auth_repo_impl.h"
#include "repositories/auth_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
mf_auth_repo_find_by_username(void* pool, const char* username, mf_user_t* out_user)
{
    if (!pool || !username || !username[0] || !out_user) {
        return -1;
    }
    memset(out_user, 0, sizeof(*out_user));

    csilk_json_t* res = user_find_by_username((csilk_db_pool_t*)pool, username);
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
    s = csilk_json_get_string(row, "password_hash");
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

    csilk_json_t* res = user_get_by_id((csilk_db_pool_t*)pool, user_id);
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
    int64_t id = user_insert((csilk_db_pool_t*)pool, username, password_hash);
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
    return user_update_password((csilk_db_pool_t*)pool, user_id, password_hash);
}

int
mf_auth_repo_update_token_version(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    return user_update_token_version((csilk_db_pool_t*)pool, user_id);
}

int
mf_auth_repo_set_totp_secret(void* pool, int64_t user_id, const char* secret)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    return user_set_totp_secret((csilk_db_pool_t*)pool, user_id, secret);
}

int
mf_auth_repo_enable_totp(void* pool, int64_t user_id, const char* backup_codes_json)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    return user_enable_totp((csilk_db_pool_t*)pool, user_id, backup_codes_json);
}

int
mf_auth_repo_disable_totp(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    return user_disable_totp((csilk_db_pool_t*)pool, user_id);
}

int
mf_auth_repo_update_backup_codes(void* pool, int64_t user_id, const char* backup_codes_json)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    return user_update_backup_codes((csilk_db_pool_t*)pool, user_id, backup_codes_json);
}

int
mf_auth_repo_count(void* pool)
{
    if (!pool) {
        return 0;
    }
    return user_count((csilk_db_pool_t*)pool);
}

int
mf_auth_repo_is_initialized(void* pool)
{
    if (!pool) {
        return 0;
    }
    return system_is_initialized((csilk_db_pool_t*)pool);
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

    csilk_json_t* res = user_find_by_oauth((csilk_db_pool_t*)pool, provider, oauth_id);
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

    int64_t id = user_create_oauth((csilk_db_pool_t*)pool, username, provider, oauth_id);
    if (id <= 0) {
        *out_id = -1;
        return -1;
    }
    *out_id = id;
    return 0;
}
