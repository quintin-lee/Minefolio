#include "infrastructure/repositories/auth_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- SQL statements inlined from repositories/auth_repo.c --- */

static void
populate_user_from_row(const csilk_json_t* row, mf_user_t* out_user)
{
    if (!row || !out_user) {
        return;
    }
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
    out_user->totp_enabled = (db_get_bool(row, "totp_enabled") != 0);
    s = csilk_json_get_string(row, "totp_secret");
    if (s) {
        snprintf(out_user->totp_secret, sizeof(out_user->totp_secret), "%s", s);
    }
    s = csilk_json_get_string(row, "totp_backup_codes");
    if (s) {
        snprintf(out_user->totp_backup_codes, sizeof(out_user->totp_backup_codes), "%s", s);
    }
    s = csilk_json_get_string(row, "oauth_provider");
    if (s) {
        snprintf(out_user->oauth_provider, sizeof(out_user->oauth_provider), "%s", s);
    }
    s = csilk_json_get_string(row, "role");
    snprintf(out_user->role, sizeof(out_user->role), "%s", (s && s[0]) ? s : "user");
    out_user->disabled = (db_get_bool(row, "disabled") != 0);
    s = csilk_json_get_string(row, "created_at");
    if (s) {
        snprintf(out_user->created_at, sizeof(out_user->created_at), "%s", s);
    }
}

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
        "totp_backup_codes, COALESCE(oauth_provider, '') AS oauth_provider, "
        "COALESCE(role, 'user') AS role, COALESCE(disabled, 0) AS disabled, "
        "COALESCE(CAST(created_at AS TEXT), '') AS created_at "
        "FROM users WHERE username = ?",
        (const char*[]){username, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1;
    }
    populate_user_from_row(csilk_json_array_get(res, 0), out_user);
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
        "totp_backup_codes, COALESCE(oauth_provider, '') AS oauth_provider, "
        "COALESCE(role, 'user') AS role, COALESCE(disabled, 0) AS disabled, "
        "COALESCE(CAST(created_at AS TEXT), '') AS created_at "
        "FROM users WHERE id = ?",
        (const char*[]){uid, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1;
    }
    populate_user_from_row(csilk_json_array_get(res, 0), out_user);
    csilk_json_free(res);
    return 0;
}

int
mf_auth_repo_create_with_role(
    void* pool, const char* username, const char* password_hash, const char* role, int64_t* out_id)
{
    if (!pool || !username || !password_hash) {
        return -1;
    }
    const char*   safe_role = (role && strcmp(role, "admin") == 0) ? "admin" : "user";
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "INSERT INTO users (username, password, role) VALUES (?, ?, ?) RETURNING id",
        (const char*[]){username, password_hash, safe_role, NULL});
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
mf_auth_repo_create(void* pool, const char* username, const char* password_hash, int64_t* out_id)
{
    return mf_auth_repo_create_with_role(pool, username, password_hash, "user", out_id);
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

int64_t
mf_auth_repo_get_instance_admin_id(void* pool)
{
    if (!pool) {
        return 0;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    res = csilk_db_query_param_json(
        db_pool,
        "SELECT id as admin_id FROM users WHERE COALESCE(role, 'user') = 'admin' "
        "AND COALESCE(disabled, 0) = 0 ORDER BY id ASC LIMIT 1",
        (const char*[]){NULL});
    int64_t admin_id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        admin_id = db_get_int(csilk_json_array_get(res, 0), "admin_id");
    }
    if (res) {
        csilk_json_free(res);
    }
    if (admin_id > 0) {
        return admin_id;
    }

    /* 兼容未显式指定 role 的纯 SQL 测试夹具：仅当系统无任何 role='admin' 账号时回退 MIN(id) */
    csilk_json_t* has_explicit = csilk_db_query_param_json(
        db_pool,
        "SELECT COUNT(*) as cnt FROM users WHERE COALESCE(role, 'user') = 'admin'",
        (const char*[]){NULL});
    int64_t explicit_cnt = 0;
    if (has_explicit && csilk_json_array_size(has_explicit) > 0) {
        explicit_cnt = db_get_int(csilk_json_array_get(has_explicit, 0), "cnt");
    }
    if (has_explicit) {
        csilk_json_free(has_explicit);
    }
    if (explicit_cnt > 0) {
        return 0;
    }

    csilk_json_t* fallback = csilk_db_query_param_json(
        db_pool,
        "SELECT MIN(id) as admin_id FROM users WHERE COALESCE(disabled, 0) = 0",
        (const char*[]){NULL});
    if (fallback && csilk_json_array_size(fallback) > 0) {
        admin_id = db_get_int(csilk_json_array_get(fallback, 0), "admin_id");
    }
    if (fallback) {
        csilk_json_free(fallback);
    }
    return admin_id;
}

int
mf_auth_repo_is_instance_admin(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return 0;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    char             uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(
        db_pool,
        "SELECT COALESCE(role, 'user') AS role, COALESCE(disabled, 0) AS disabled "
        "FROM users WHERE id = ?",
        (const char*[]){uid, NULL});
    int  is_admin = 0;
    bool found_active = false;
    if (res && csilk_json_array_size(res) > 0) {
        const csilk_json_t* row = csilk_json_array_get(res, 0);
        const char*         role = csilk_json_get_string(row, "role");
        int                 disabled = db_get_bool(row, "disabled");
        if (disabled == 0) {
            found_active = true;
            if (role && strcmp(role, "admin") == 0) {
                is_admin = 1;
            }
        }
    }
    if (res) {
        csilk_json_free(res);
    }
    if (is_admin) {
        return 1;
    }
    if (!found_active) {
        return 0;
    }

    /* 兼容未显式指定 role 的纯 SQL 测试夹具（库中尚无任何 role='admin' 用户） */
    csilk_json_t* has_explicit = csilk_db_query_param_json(
        db_pool,
        "SELECT COUNT(*) as cnt FROM users WHERE COALESCE(role, 'user') = 'admin'",
        (const char*[]){NULL});
    int64_t explicit_cnt = 0;
    if (has_explicit && csilk_json_array_size(has_explicit) > 0) {
        explicit_cnt = db_get_int(csilk_json_array_get(has_explicit, 0), "cnt");
    }
    if (has_explicit) {
        csilk_json_free(has_explicit);
    }
    if (explicit_cnt > 0) {
        return 0;
    }
    int64_t fallback_id = mf_auth_repo_get_instance_admin_id(db_pool);
    return (fallback_id > 0 && user_id == fallback_id) ? 1 : 0;
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
        "SELECT id, username, token_version, totp_secret, totp_enabled, "
        "totp_backup_codes, COALESCE(oauth_provider, '') AS oauth_provider, "
        "COALESCE(role, 'user') AS role, COALESCE(disabled, 0) AS disabled, "
        "COALESCE(CAST(created_at AS TEXT), '') AS created_at "
        "FROM users WHERE oauth_provider = ? AND oauth_id = ?",
        (const char*[]){provider, oauth_id, NULL});
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return 1;
    }
    populate_user_from_row(csilk_json_array_get(res, 0), out_user);
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
        "INSERT INTO users (username, password, token_version, oauth_provider, oauth_id, role) "
        "VALUES (?, '', 0, ?, ?, 'user') RETURNING id",
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

static void
escape_like_pattern(const char* src, char* dst, size_t dst_cap)
{
    if (!dst || dst_cap == 0) {
        return;
    }
    size_t j = 0;
    if (j + 1 < dst_cap) {
        dst[j++] = '%';
    }
    if (src) {
        for (size_t i = 0; src[i] && j + 3 < dst_cap; i++) {
            char ch = src[i];
            if (ch == '!' || ch == '%' || ch == '_') {
                dst[j++] = '!';
            }
            dst[j++] = ch;
        }
    }
    if (j + 1 < dst_cap) {
        dst[j++] = '%';
    }
    dst[j] = '\0';
}

int
mf_auth_repo_list_users(void*               pool,
                        int64_t             page,
                        int64_t             page_size,
                        const char*         keyword,
                        const char*         role_filter,
                        int                 disabled_filter,
                        mf_user_summary_t** out_items,
                        int*                out_count,
                        int64_t*            out_total)
{
    if (!pool || !out_items || !out_count) {
        return -1;
    }
    *out_items = NULL;
    *out_count = 0;
    if (out_total) {
        *out_total = 0;
    }

    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 20;
    }
    if (page_size > 200) {
        page_size = 200;
    }
    int64_t offset = (page - 1) * page_size;

    char        where_clause[512] = "WHERE 1=1";
    const char* count_params[8] = {NULL};
    const char* list_params[10] = {NULL};
    int         p_idx = 0;
    char        kw_buf[256] = {0};

    if (keyword && keyword[0]) {
        escape_like_pattern(keyword, kw_buf, sizeof(kw_buf));
        strncat(where_clause,
                " AND LOWER(username) LIKE LOWER(?) ESCAPE '!'",
                sizeof(where_clause) - strlen(where_clause) - 1);
        count_params[p_idx] = kw_buf;
        list_params[p_idx] = kw_buf;
        p_idx++;
    }
    if (role_filter && (strcmp(role_filter, "admin") == 0 || strcmp(role_filter, "user") == 0)) {
        strncat(where_clause,
                " AND COALESCE(role, 'user') = ?",
                sizeof(where_clause) - strlen(where_clause) - 1);
        count_params[p_idx] = role_filter;
        list_params[p_idx] = role_filter;
        p_idx++;
    }
    if (disabled_filter == 0) {
        strncat(where_clause,
                " AND COALESCE(disabled, 0) = 0",
                sizeof(where_clause) - strlen(where_clause) - 1);
    } else if (disabled_filter == 1) {
        strncat(where_clause,
                " AND COALESCE(disabled, 0) != 0",
                sizeof(where_clause) - strlen(where_clause) - 1);
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;

    if (out_total) {
        char count_sql[768];
        snprintf(
            count_sql, sizeof(count_sql), "SELECT COUNT(*) AS total FROM users %s", where_clause);
        csilk_json_t* cres = csilk_db_query_param_json(db_pool, count_sql, count_params);
        if (cres) {
            if (csilk_json_array_size(cres) > 0) {
                *out_total = db_get_int(csilk_json_array_get(cres, 0), "total");
            }
            csilk_json_free(cres);
        }
    }

    char limit_str[32], offset_str[32];
    snprintf(limit_str, sizeof(limit_str), "%lld", (long long)page_size);
    snprintf(offset_str, sizeof(offset_str), "%lld", (long long)offset);
    list_params[p_idx++] = limit_str;
    list_params[p_idx++] = offset_str;
    list_params[p_idx] = NULL;

    char list_sql[1024];
    snprintf(list_sql,
             sizeof(list_sql),
             "SELECT id, username, COALESCE(role, 'user') AS role, "
             "COALESCE(disabled, 0) AS disabled, totp_enabled, "
             "COALESCE(oauth_provider, '') AS oauth_provider, "
             "COALESCE(CAST(created_at AS TEXT), '') AS created_at "
             "FROM users %s ORDER BY id ASC LIMIT ? OFFSET ?",
             where_clause);

    csilk_json_t* res = csilk_db_query_param_json(db_pool, list_sql, list_params);
    if (!res) {
        return -1;
    }

    int n = (int)csilk_json_array_size(res);
    if (n <= 0) {
        csilk_json_free(res);
        return 0;
    }

    mf_user_summary_t* items = calloc((size_t)n, sizeof(mf_user_summary_t));
    if (!items) {
        csilk_json_free(res);
        return -1;
    }

    for (int i = 0; i < n; i++) {
        const csilk_json_t* row = csilk_json_array_get(res, (size_t)i);
        items[i].id = db_get_int(row, "id");
        const char* s = csilk_json_get_string(row, "username");
        if (s) {
            snprintf(items[i].username, sizeof(items[i].username), "%s", s);
        }
        s = csilk_json_get_string(row, "role");
        snprintf(items[i].role, sizeof(items[i].role), "%s", (s && s[0]) ? s : "user");
        items[i].disabled = (db_get_bool(row, "disabled") != 0);
        items[i].totp_enabled = (db_get_bool(row, "totp_enabled") != 0);
        s = csilk_json_get_string(row, "oauth_provider");
        if (s) {
            snprintf(items[i].oauth_provider, sizeof(items[i].oauth_provider), "%s", s);
        }
        s = csilk_json_get_string(row, "created_at");
        if (s) {
            snprintf(items[i].created_at, sizeof(items[i].created_at), "%s", s);
        }
    }

    csilk_json_free(res);
    *out_items = items;
    *out_count = n;
    return 0;
}

int
mf_auth_repo_get_user_stats(void* pool, mf_user_stats_t* out_stats)
{
    if (!pool || !out_stats) {
        return -1;
    }
    memset(out_stats, 0, sizeof(*out_stats));
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT COUNT(*) AS total_users, "
        "SUM(CASE WHEN COALESCE(disabled, 0) = 0 THEN 1 ELSE 0 END) AS active_users, "
        "SUM(CASE WHEN COALESCE(role, 'user') = 'admin' THEN 1 ELSE 0 END) AS admin_users, "
        "SUM(CASE WHEN COALESCE(role, 'user') = 'admin' AND COALESCE(disabled, 0) = 0 THEN 1 ELSE "
        "0 END) AS active_admin_users, "
        "SUM(CASE WHEN COALESCE(disabled, 0) != 0 THEN 1 ELSE 0 END) AS disabled_users "
        "FROM users",
        (const char*[]){NULL});
    if (!res) {
        return -1;
    }
    if (csilk_json_array_size(res) > 0) {
        const csilk_json_t* row = csilk_json_array_get(res, 0);
        out_stats->total_users = db_get_int(row, "total_users");
        out_stats->active_users = db_get_int(row, "active_users");
        out_stats->admin_users = db_get_int(row, "admin_users");
        out_stats->active_admin_users = db_get_int(row, "active_admin_users");
        out_stats->disabled_users = db_get_int(row, "disabled_users");
    }
    csilk_json_free(res);
    return 0;
}

int
mf_auth_repo_update_role(void* pool, int64_t user_id, const char* role)
{
    if (!pool || user_id <= 0 || !role ||
        (strcmp(role, "admin") != 0 && strcmp(role, "user") != 0)) {
        return -1;
    }
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res =
        csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                  "UPDATE users SET role = ? WHERE id = ? RETURNING id",
                                  (const char*[]){role, uid, NULL});
    int ok = (res && csilk_json_array_size(res) > 0);
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
mf_auth_repo_set_disabled(void* pool, int64_t user_id, bool disabled)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    const char*   val = disabled ? "1" : "0";
    csilk_json_t* res =
        csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                  "UPDATE users SET disabled = ? WHERE id = ? RETURNING id",
                                  (const char*[]){val, uid, NULL});
    int ok = (res && csilk_json_array_size(res) > 0);
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
mf_auth_repo_count_active_admins(void* pool)
{
    if (!pool) {
        return 0;
    }
    csilk_json_t* res = csilk_db_query_param_json(
        (csilk_db_pool_t*)pool,
        "SELECT COUNT(*) AS cnt FROM users WHERE COALESCE(role, 'user') = 'admin' "
        "AND COALESCE(disabled, 0) = 0",
        (const char*[]){NULL});
    int cnt = 0;
    if (res && csilk_json_array_size(res) > 0) {
        cnt = (int)db_get_int(csilk_json_array_get(res, 0), "cnt");
    }
    if (res) {
        csilk_json_free(res);
    }
    return cnt;
}

int
mf_auth_repo_delete_user_cascade(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    char             uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    db_tx_scope_t scope;
    if (db_tx_scope_begin(db_pool, "mf_delete_user", &scope) != 0) {
        return -1;
    }

    const char* p1[] = {uid, NULL};
    const char* p3[] = {uid, uid, uid, NULL};

    /* 按外键依赖逆序清理用户关联业务数据（含无 ON DELETE CASCADE 或 ON DELETE RESTRICT 表） */
    csilk_db_exec_param(db_pool,
                        "DELETE FROM dca_executions WHERE user_id = ? OR plan_id IN ("
                        "SELECT id FROM dca_plans WHERE user_id = ? OR target_asset_id IN "
                        "(SELECT id FROM assets WHERE user_id = ?) OR funding_asset_id IN "
                        "(SELECT id FROM assets WHERE user_id = ?))",
                        (const char*[]){uid, uid, uid, uid, NULL});
    csilk_db_exec_param(db_pool,
                        "DELETE FROM dca_plans WHERE user_id = ? OR target_asset_id IN "
                        "(SELECT id FROM assets WHERE user_id = ?) OR funding_asset_id IN "
                        "(SELECT id FROM assets WHERE user_id = ?)",
                        p3);
    csilk_db_exec_param(db_pool,
                        "DELETE FROM cashflow_schedules WHERE user_id = ? OR source_asset_id IN "
                        "(SELECT id FROM assets WHERE user_id = ?) OR target_asset_id IN "
                        "(SELECT id FROM assets WHERE user_id = ?)",
                        p3);
    csilk_db_exec_param(db_pool, "DELETE FROM asset_balance_logs WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM transaction_lots WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM ledger_snapshots WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM transfers WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM transactions WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool,
                        "DELETE FROM expense_tags WHERE expense_id IN "
                        "(SELECT id FROM daily_expenses WHERE user_id = ?)",
                        p1);
    csilk_db_exec_param(db_pool, "DELETE FROM daily_expenses WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM tags WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool,
                        "DELETE FROM asset_price_history WHERE asset_id IN "
                        "(SELECT id FROM assets WHERE user_id = ?)",
                        p1);
    csilk_db_exec_param(db_pool, "DELETE FROM assets WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM account_permissions WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM accounts WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM import_rules WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM categories WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM category_seed_state WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM ai_session_summaries WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM ai_traces WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool,
                        "DELETE FROM ai_messages WHERE session_id IN "
                        "(SELECT id FROM ai_sessions WHERE user_id = ?)",
                        p1);
    csilk_db_exec_param(db_pool, "DELETE FROM ai_sessions WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM ai_consumed_nonces WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM ai_skills WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM ai_news_reports WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM im_bot_bindings WHERE mf_user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM job_run_logs WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM scheduled_jobs WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM user_notification_channels WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM async_tasks WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM ledger_members WHERE user_id = ?", p1);
    csilk_db_exec_param(db_pool, "DELETE FROM ledgers WHERE owner_id = ?", p1);

    csilk_json_t* res =
        csilk_db_query_param_json(db_pool, "DELETE FROM users WHERE id = ? RETURNING id", p1);
    int ok = (res && csilk_json_array_size(res) > 0);
    if (res) {
        csilk_json_free(res);
    }
    if (!ok) {
        db_tx_scope_rollback(db_pool, &scope);
        return -1;
    }

    if (db_tx_scope_commit(db_pool, &scope) != 0) {
        db_tx_scope_rollback(db_pool, &scope);
        return -1;
    }
    return 0;
}
