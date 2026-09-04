#include "repositories/user_repository.h"
#include "infrastructure/database/statement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
user_repo_find_by_id(mf_db_t* db, int64_t id, user_record_t* out_user)
{
    if (!db || !out_user) {
        return -1;
    }
    memset(out_user, 0, sizeof(*out_user));

    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(db,
                             "SELECT id, username, password, email, "
                             "COALESCE(totp_secret, '') AS totp_secret, "
                             "COALESCE(totp_enabled, 0) AS totp_enabled, "
                             "COALESCE(totp_backup_codes, '') AS totp_backup_codes, "
                             "created_at, updated_at "
                             "FROM users WHERE id = ?;",
                             &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    mf_stmt_bind_int64(stmt, 1, id);
    mf_result_t* res = NULL;
    rc = mf_stmt_query(stmt, &res);
    if (rc != 0 || !res) {
        mf_stmt_close(stmt);
        return -1;
    }

    if (!mf_result_next(res)) {
        mf_result_free(res);
        mf_stmt_close(stmt);
        return 1; /* 不存在 */
    }

    out_user->id = mf_result_get_int64(res, "id");
    snprintf(out_user->username, sizeof(out_user->username), "%s",
             mf_result_get_text(res, "username"));
    snprintf(out_user->password_hash, sizeof(out_user->password_hash), "%s",
             mf_result_get_text(res, "password"));
    snprintf(out_user->email, sizeof(out_user->email), "%s", mf_result_get_text(res, "email"));
    snprintf(out_user->totp_secret, sizeof(out_user->totp_secret), "%s",
             mf_result_get_text(res, "totp_secret"));
    out_user->totp_enabled = mf_result_get_bool(res, "totp_enabled");
    snprintf(out_user->totp_backup_codes, sizeof(out_user->totp_backup_codes), "%s",
             mf_result_get_text(res, "totp_backup_codes"));
    snprintf(out_user->created_at, sizeof(out_user->created_at), "%s",
             mf_result_get_text(res, "created_at"));
    snprintf(out_user->updated_at, sizeof(out_user->updated_at), "%s",
             mf_result_get_text(res, "updated_at"));

    mf_result_free(res);
    mf_stmt_close(stmt);
    return 0;
}

int
user_repo_find_by_username(mf_db_t* db, const char* username, user_record_t* out_user)
{
    if (!db || !username || !out_user) {
        return -1;
    }
    memset(out_user, 0, sizeof(*out_user));

    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(db,
                             "SELECT id, username, password, email, "
                             "COALESCE(totp_secret, '') AS totp_secret, "
                             "COALESCE(totp_enabled, 0) AS totp_enabled, "
                             "COALESCE(totp_backup_codes, '') AS totp_backup_codes, "
                             "created_at, updated_at "
                             "FROM users WHERE username = ?;",
                             &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    mf_stmt_bind_text(stmt, 1, username);
    mf_result_t* res = NULL;
    rc = mf_stmt_query(stmt, &res);
    if (rc != 0 || !res) {
        mf_stmt_close(stmt);
        return -1;
    }

    if (!mf_result_next(res)) {
        mf_result_free(res);
        mf_stmt_close(stmt);
        return 1; /* 不存在 */
    }

    out_user->id = mf_result_get_int64(res, "id");
    snprintf(out_user->username, sizeof(out_user->username), "%s",
             mf_result_get_text(res, "username"));
    snprintf(out_user->password_hash, sizeof(out_user->password_hash), "%s",
             mf_result_get_text(res, "password"));
    snprintf(out_user->email, sizeof(out_user->email), "%s", mf_result_get_text(res, "email"));
    snprintf(out_user->totp_secret, sizeof(out_user->totp_secret), "%s",
             mf_result_get_text(res, "totp_secret"));
    out_user->totp_enabled = mf_result_get_bool(res, "totp_enabled");
    snprintf(out_user->totp_backup_codes, sizeof(out_user->totp_backup_codes), "%s",
             mf_result_get_text(res, "totp_backup_codes"));
    snprintf(out_user->created_at, sizeof(out_user->created_at), "%s",
             mf_result_get_text(res, "created_at"));
    snprintf(out_user->updated_at, sizeof(out_user->updated_at), "%s",
             mf_result_get_text(res, "updated_at"));

    mf_result_free(res);
    mf_stmt_close(stmt);
    return 0;
}

int64_t
user_repo_insert(mf_db_t* db, const char* username, const char* password_hash)
{
    if (!db || !username || !password_hash) {
        return -1;
    }

    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(
        db, "INSERT INTO users (username, password, email) VALUES (?, ?, '') RETURNING id;", &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }

    mf_stmt_bind_text(stmt, 1, username);
    mf_stmt_bind_text(stmt, 2, password_hash);

    mf_result_t* res = NULL;
    rc = mf_stmt_query(stmt, &res);
    int64_t id = 0;
    if (rc == 0 && res && mf_result_next(res)) {
        id = mf_result_get_int64(res, "id");
    }
    if (res) {
        mf_result_free(res);
    }
    mf_stmt_close(stmt);
    return id;
}

int
user_repo_update_password(mf_db_t* db, int64_t id, const char* new_hash)
{
    if (!db || !new_hash) {
        return -1;
    }
    mf_stmt_t* stmt = NULL;
    int rc = mf_stmt_prepare(db, "UPDATE users SET password = ? WHERE id = ?;", &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }
    mf_stmt_bind_text(stmt, 1, new_hash);
    mf_stmt_bind_int64(stmt, 2, id);
    int64_t affected = 0;
    rc = mf_stmt_execute(stmt, &affected);
    mf_stmt_close(stmt);
    return (rc == 0 && affected > 0) ? 0 : -1;
}

int
user_repo_update_totp(mf_db_t*    db,
                      int64_t     id,
                      const char* secret,
                      bool        enabled,
                      const char* backup_codes)
{
    if (!db) {
        return -1;
    }
    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(
        db,
        "UPDATE users SET totp_secret = ?, totp_enabled = ?, totp_backup_codes = ? WHERE id = ?;",
        &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }
    mf_stmt_bind_text(stmt, 1, secret ? secret : "");
    mf_stmt_bind_bool(stmt, 2, enabled);
    mf_stmt_bind_text(stmt, 3, backup_codes ? backup_codes : "");
    mf_stmt_bind_int64(stmt, 4, id);
    int64_t affected = 0;
    rc = mf_stmt_execute(stmt, &affected);
    mf_stmt_close(stmt);
    return (rc == 0 && affected > 0) ? 0 : -1;
}

int
user_repo_count(mf_db_t* db, int64_t* out_count)
{
    if (!db || !out_count) {
        return -1;
    }
    mf_stmt_t* stmt = NULL;
    int        rc = mf_stmt_prepare(db, "SELECT COUNT(*) AS cnt FROM users;", &stmt);
    if (rc != 0 || !stmt) {
        return -1;
    }
    mf_result_t* res = NULL;
    rc = mf_stmt_query(stmt, &res);
    int64_t cnt = 0;
    if (rc == 0 && res && mf_result_next(res)) {
        cnt = mf_result_get_int64(res, "cnt");
    }
    if (res) {
        mf_result_free(res);
    }
    mf_stmt_close(stmt);
    *out_count = cnt;
    return 0;
}
