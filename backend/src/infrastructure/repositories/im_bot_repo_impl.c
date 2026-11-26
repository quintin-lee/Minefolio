/** @file im_bot_repo_impl.c @brief IM 机器人绑定与实例仓储 (im_bot_bindings / im_bot_instances) */

#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define IM_INSTANCE_COLS                                                                           \
    "id, mf_user_id, platform, adapter, ilink_bot_id, ilink_user_id, im_user_id, status, "         \
    "CASE WHEN status IN ('pending_login','qr_ready','scanned','need_verifycode') "                \
    "THEN qr_content ELSE NULL END AS qr_content, "                                                \
    "CASE WHEN status IN ('pending_login','qr_ready','scanned','need_verifycode') "                \
    "THEN qr_expires_at ELSE NULL END AS qr_expires_at, "                                          \
    "login_epoch, verify_attempts, capability, enabled, last_error, last_seen_at, "                \
    "last_inbound_at, "                                                                            \
    "created_at, updated_at"

static void
im_id_str(int64_t id, char* out, size_t cap)
{
    snprintf(out, cap, "%lld", (long long)id);
}

static csilk_json_t*
im_instance_query(csilk_db_pool_t* pool, const char* where, const char* const* params)
{
    char sql[1024];
    SQL_FMT(sql, sizeof(sql), "SELECT " IM_INSTANCE_COLS " FROM im_bot_instances WHERE %s", where);
    return csilk_db_query_param_json(pool, sql, (const char**)params);
}

static csilk_json_t*
im_instance_one(csilk_db_pool_t* pool, const char* where, const char* const* params)
{
    csilk_json_t* res = im_instance_query(pool, where, params);
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return NULL;
    }
    csilk_json_t* copy = csilk_json_copy(csilk_json_array_get(res, 0));
    csilk_json_free(res);
    return copy;
}

static int
im_valid_digits(const char* code, size_t min_len, size_t max_len)
{
    if (!code) {
        return 0;
    }
    size_t n = strlen(code);
    if (n < min_len || n > max_len) {
        return 0;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!isdigit((unsigned char)code[i])) {
            return 0;
        }
    }
    return 1;
}

int64_t
mf_im_bot_repo_lookup(csilk_db_pool_t* pool,
                      const char*      platform,
                      const char*      im_user_id,
                      char*            scope_out,
                      size_t           scope_sz)
{
    if (!pool || !platform || !platform[0] || !im_user_id || !im_user_id[0]) {
        return 0;
    }
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT mf_user_id, scope FROM im_bot_bindings "
                                  "WHERE platform=? AND im_user_id=? AND enabled=1 LIMIT 1",
                                  (const char*[]){platform, im_user_id, NULL});
    int64_t mf_user_id = 0;
    if (res && csilk_json_is_array(res) && csilk_json_array_size(res) > 0) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        mf_user_id = db_get_int(row, "mf_user_id");
        if (scope_out && scope_sz > 0) {
            const char* scope = csilk_json_get_string(row, "scope");
            snprintf(scope_out, scope_sz, "%s", scope ? scope : "readonly");
        }
    }
    if (res) {
        csilk_json_free(res);
    }
    return mf_user_id;
}

int
mf_im_bot_repo_upsert(csilk_db_pool_t* pool,
                      const char*      platform,
                      const char*      im_user_id,
                      int64_t          mf_user_id,
                      const char*      scope)
{
    if (!pool || !platform || platform[0] == 0 || !im_user_id || im_user_id[0] == 0 ||
        mf_user_id <= 0) {
        return -1;
    }
    const char* eff_scope = (scope && scope[0]) ? scope : "readonly";
    char        uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)mf_user_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO im_bot_bindings (platform, im_user_id, mf_user_id, scope, enabled) "
        "VALUES (?, ?, ?, ?, 1) "
        "ON CONFLICT(platform, im_user_id) DO UPDATE SET scope = excluded.scope, enabled = 1 "
        "WHERE im_bot_bindings.mf_user_id = excluded.mf_user_id",
        (const char*[]){platform, im_user_id, uid_str, eff_scope, NULL});
    int rc = 0;
    if (res) {
        csilk_json_free(res);
    }
    return rc;
}

csilk_json_t*
mf_im_bot_repo_list_for_user(csilk_db_pool_t* pool, int64_t mf_user_id)
{
    if (!pool || mf_user_id <= 0) {
        return csilk_json_array();
    }
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)mf_user_id);
    return csilk_db_query_param_json(
        pool,
        "SELECT platform, im_user_id, scope, enabled, created_at "
        "FROM im_bot_bindings WHERE mf_user_id=? ORDER BY platform, im_user_id",
        (const char*[]){id_str, NULL});
}

int
mf_im_bot_repo_unbind(csilk_db_pool_t* pool,
                      const char*      platform,
                      const char*      im_user_id,
                      int64_t          mf_user_id)
{
    if (!pool || !platform || platform[0] == 0 || !im_user_id || im_user_id[0] == 0 ||
        mf_user_id <= 0) {
        return -1;
    }
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)mf_user_id);
    int rc = csilk_db_exec_param(
        pool,
        "DELETE FROM im_bot_bindings WHERE platform=? AND im_user_id=? AND mf_user_id=?",
        (const char*[]){platform, im_user_id, id_str, NULL});
    return rc;
}

csilk_json_t*
mf_im_bot_instance_list(csilk_db_pool_t* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return csilk_json_array();
    }
    char uid[32];
    im_id_str(user_id, uid, sizeof(uid));
    csilk_json_t* res =
        im_instance_query(pool, "mf_user_id=? ORDER BY id DESC", (const char*[]){uid, NULL});
    return res ? res : csilk_json_array();
}

csilk_json_t*
mf_im_bot_instance_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return NULL;
    }
    char uid[32], sid[32];
    im_id_str(user_id, uid, sizeof(uid));
    im_id_str(id, sid, sizeof(sid));
    return im_instance_one(pool, "mf_user_id=? AND id=?", (const char*[]){uid, sid, NULL});
}

csilk_json_t*
mf_im_bot_instance_get_any(csilk_db_pool_t* pool, int64_t id)
{
    if (!pool || id <= 0) {
        return NULL;
    }
    char sid[32];
    im_id_str(id, sid, sizeof(sid));
    return im_instance_one(pool, "id=?", (const char*[]){sid, NULL});
}

csilk_json_t*
mf_im_bot_instance_list_all(csilk_db_pool_t* pool)
{
    if (!pool) {
        return csilk_json_array();
    }
    csilk_json_t* res = im_instance_query(pool, "1=1 ORDER BY id", (const char*[]){NULL});
    return res ? res : csilk_json_array();
}

int64_t
mf_im_bot_instance_create(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          const char*      platform,
                          const char*      adapter)
{
    if (!pool || user_id <= 0) {
        return -1;
    }
    const char* plat = (platform && platform[0]) ? platform : "wechat";
    const char* adp = (adapter && adapter[0]) ? adapter : "ilink";
    char        uid[32];
    im_id_str(user_id, uid, sizeof(uid));

    csilk_json_t* exists = csilk_db_query_param_json(
        pool,
        "SELECT id FROM im_bot_instances WHERE mf_user_id=? AND platform=? AND adapter=? LIMIT 1",
        (const char*[]){uid, plat, adp, NULL});
    if (exists) {
        size_t n = csilk_json_is_array(exists) ? csilk_json_array_size(exists) : 0;
        csilk_json_free(exists);
        if (n > 0) {
            return -2;
        }
    }

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO im_bot_instances (mf_user_id, platform, adapter, status) "
        "VALUES (?, ?, ?, 'pending_login') RETURNING id",
        (const char*[]){uid, plat, adp, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        csilk_json_t* again =
            csilk_db_query_param_json(pool,
                                      "SELECT id FROM im_bot_instances WHERE mf_user_id=? AND "
                                      "platform=? AND adapter=? LIMIT 1",
                                      (const char*[]){uid, plat, adp, NULL});
        int64_t rc = -1;
        if (again && csilk_json_is_array(again) && csilk_json_array_size(again) > 0) {
            rc = -2;
        }
        if (again) {
            csilk_json_free(again);
        }
        return rc;
    }
    int64_t id = db_get_int(csilk_json_array_get(res, 0), "id");
    csilk_json_free(res);
    return id;
}

int
mf_im_bot_instance_relogin(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return -1;
    }
    char uid[32], sid[32];
    im_id_str(user_id, uid, sizeof(uid));
    im_id_str(id, sid, sizeof(sid));
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE im_bot_instances SET status='pending_login', login_epoch=login_epoch+1, "
        "qr_content=NULL, qr_expires_at=NULL, pending_verify_code=NULL, "
        "pending_verify_expires_at=NULL, verify_attempts=0, last_error=NULL, "
        "updated_at=CURRENT_TIMESTAMP WHERE id=? AND mf_user_id=? RETURNING id",
        (const char*[]){sid, uid, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -3;
    }
    csilk_json_free(res);
    return 0;
}

int
mf_im_bot_instance_set_enabled(csilk_db_pool_t* pool, int64_t user_id, int64_t id, int enabled)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return -1;
    }
    char uid[32], sid[32];
    im_id_str(user_id, uid, sizeof(uid));
    im_id_str(id, sid, sizeof(sid));
    const char* sql =
        enabled ? "UPDATE im_bot_instances SET enabled=1, status='pending_login', "
                  "login_epoch=login_epoch+1, "
                  "qr_content=NULL, qr_expires_at=NULL, pending_verify_code=NULL, "
                  "pending_verify_expires_at=NULL, verify_attempts=0, last_error=NULL, "
                  "updated_at=CURRENT_TIMESTAMP WHERE id=? AND mf_user_id=? RETURNING id"
                : "UPDATE im_bot_instances SET enabled=0, status='disabled', qr_content=NULL, "
                  "qr_expires_at=NULL, pending_verify_code=NULL, pending_verify_expires_at=NULL, "
                  "updated_at=CURRENT_TIMESTAMP WHERE id=? AND mf_user_id=? RETURNING id";
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){sid, uid, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -3;
    }
    csilk_json_free(res);
    return 0;
}

int
mf_im_bot_instance_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return -1;
    }
    char uid[32], sid[32];
    im_id_str(user_id, uid, sizeof(uid));
    im_id_str(id, sid, sizeof(sid));
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_instance_delete", &scope) != 0) {
        return -1;
    }
    csilk_json_t* row = csilk_db_query_param_json(
        pool,
        "SELECT platform, im_user_id FROM im_bot_instances WHERE id=? AND mf_user_id=? LIMIT 1",
        (const char*[]){sid, uid, NULL});
    if (!row || !csilk_json_is_array(row) || csilk_json_array_size(row) == 0) {
        if (row) {
            csilk_json_free(row);
        }
        db_tx_scope_rollback(pool, &scope);
        return -3;
    }
    char          plat[64] = "wechat";
    char          old[256] = {0};
    csilk_json_t* r0 = csilk_json_array_get(row, 0);
    const char*   plat_ptr = csilk_json_get_string(r0, "platform");
    if (plat_ptr && plat_ptr[0]) {
        snprintf(plat, sizeof(plat), "%s", plat_ptr);
    }
    const char* old_ptr = csilk_json_get_string(r0, "im_user_id");
    if (old_ptr && old_ptr[0]) {
        snprintf(old, sizeof(old), "%s", old_ptr);
    }
    csilk_json_free(row);

    if (old[0]) {
        csilk_db_exec_param(pool,
                            "DELETE FROM im_bot_bindings WHERE platform=? AND im_user_id=? "
                            "AND mf_user_id=?",
                            (const char*[]){plat, old, uid, NULL});
    }
    csilk_json_t* deleted = csilk_db_query_param_json(
        pool,
        "DELETE FROM im_bot_instances WHERE id=? AND mf_user_id=? RETURNING id",
        (const char*[]){sid, uid, NULL});
    int rc = 0;
    if (!deleted || !csilk_json_is_array(deleted) || csilk_json_array_size(deleted) == 0) {
        rc = -3;
    }
    if (deleted) {
        csilk_json_free(deleted);
    }
    if (rc == 0 && db_tx_scope_commit(pool, &scope) != 0) {
        return -1;
    }
    if (rc != 0) {
        db_tx_scope_rollback(pool, &scope);
    }
    return rc;
}

int
mf_im_bot_instance_submit_verify_code(csilk_db_pool_t* pool,
                                      int64_t          user_id,
                                      int64_t          id,
                                      const char*      code)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return -1;
    }
    if (!im_valid_digits(code, 4, 8)) {
        return -2;
    }
    char uid[32], sid[32];
    im_id_str(user_id, uid, sizeof(uid));
    im_id_str(id, sid, sizeof(sid));
    const char* exp = db_is_postgres() ? "(CURRENT_TIMESTAMP + INTERVAL '120 seconds')"
                                       : "datetime('now','+120 seconds')";
    char        sql[1024];
    SQL_FMT(sql,
            sizeof(sql),
            "UPDATE im_bot_instances SET pending_verify_code=?, pending_verify_expires_at=%s, "
            "verify_attempts=verify_attempts+1, updated_at=CURRENT_TIMESTAMP "
            "WHERE id=? AND mf_user_id=? AND enabled=1 "
            "AND status IN ('pending_login','qr_ready','scanned','need_verifycode') "
            "AND verify_attempts<10 RETURNING id",
            exp);
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){code, sid, uid, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        csilk_json_t* chk = csilk_db_query_param_json(
            pool,
            "SELECT status, enabled, verify_attempts FROM im_bot_instances "
            "WHERE id=? AND mf_user_id=? LIMIT 1",
            (const char*[]){sid, uid, NULL});
        if (!chk || !csilk_json_is_array(chk) || csilk_json_array_size(chk) == 0) {
            if (chk) {
                csilk_json_free(chk);
            }
            return -3;
        }
        csilk_json_t* row = csilk_json_array_get(chk, 0);
        const char*   st = csilk_json_get_string(row, "status");
        int           en = db_get_bool(row, "enabled");
        int64_t       attempts = db_get_int(row, "verify_attempts");
        int           waiting = en && st &&
                                (strcmp(st, "pending_login") == 0 || strcmp(st, "qr_ready") == 0 ||
                                 strcmp(st, "scanned") == 0 || strcmp(st, "need_verifycode") == 0);
        csilk_json_free(chk);
        if (waiting && attempts >= 10) {
            return -4;
        }
        return -3;
    }
    csilk_json_free(res);
    return 0;
}

int
mf_im_bot_instance_consume_verify_code(csilk_db_pool_t* pool, int64_t id, char* out, size_t out_cap)
{
    if (!pool || id <= 0 || !out || out_cap == 0) {
        return -1;
    }
    out[0] = 0;
    char sid[32];
    im_id_str(id, sid, sizeof(sid));
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_instance_consume_pin", &scope) != 0) {
        return -1;
    }
    char sql[512];
    SQL_FMT(sql,
            sizeof(sql),
            "SELECT pending_verify_code FROM im_bot_instances "
            "WHERE id=? AND enabled=1 AND pending_verify_code IS NOT NULL "
            "AND pending_verify_expires_at > CURRENT_TIMESTAMP LIMIT 1%s",
            db_is_postgres() ? " FOR UPDATE" : "");
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){sid, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        db_tx_scope_rollback(pool, &scope);
        return 0;
    }
    const char* code = csilk_json_get_string(csilk_json_array_get(res, 0), "pending_verify_code");
    if (!code || !code[0]) {
        csilk_json_free(res);
        db_tx_scope_rollback(pool, &scope);
        return 0;
    }
    snprintf(out, out_cap, "%s", code);
    csilk_json_free(res);
    csilk_json_t* cleared = csilk_db_query_param_json(
        pool,
        "UPDATE im_bot_instances SET pending_verify_code=NULL, pending_verify_expires_at=NULL, "
        "updated_at=CURRENT_TIMESTAMP WHERE id=? RETURNING id",
        (const char*[]){sid, NULL});
    int rc = 1;
    if (!cleared || !csilk_json_is_array(cleared) || csilk_json_array_size(cleared) == 0) {
        rc = -1;
    }
    if (cleared) {
        csilk_json_free(cleared);
    }
    if (rc == 1 && db_tx_scope_commit(pool, &scope) != 0) {
        out[0] = 0;
        return -1;
    }
    if (rc != 1) {
        out[0] = 0;
        db_tx_scope_rollback(pool, &scope);
    }
    return rc;
}

int
mf_im_bot_instance_update_status(csilk_db_pool_t* pool,
                                 int64_t          id,
                                 const char*      status,
                                 const char*      qr_content,
                                 const char*      qr_expires_at,
                                 const char*      last_error)
{
    if (!pool || id <= 0 || !status || !status[0]) {
        return -1;
    }
    char sid[32];
    im_id_str(id, sid, sizeof(sid));
    const char*   drv = csilk_db_pool_get_driver_name(pool);
    const bool    is_pg = drv && strcmp(drv, "postgres") == 0;
    const char*   qr_exp_cast = is_pg ? "NULLIF(?,'')::timestamptz" : "NULLIF(?,'')";
    csilk_json_t* res = NULL;
    if (strcmp(status, "session_expired") == 0 || strcmp(status, "expired") == 0 ||
        strcmp(status, "error") == 0) {
        res = csilk_db_query_param_json(
            pool,
            "UPDATE im_bot_instances SET status=?, qr_content=NULL, qr_expires_at=NULL, "
            "pending_verify_code=NULL, pending_verify_expires_at=NULL, verify_attempts=0, "
            "last_error=NULLIF(?,''), last_seen_at=CURRENT_TIMESTAMP, "
            "updated_at=CURRENT_TIMESTAMP WHERE id=? RETURNING id",
            (const char*[]){status, last_error ? last_error : "", sid, NULL});
    } else if (strcmp(status, "scanned") == 0 || strcmp(status, "need_verifycode") == 0) {
        char sql[512];
        snprintf(sql,
                 sizeof(sql),
                 "UPDATE im_bot_instances SET status=?, "
                 "qr_content=COALESCE(NULLIF(?,''), qr_content), "
                 "qr_expires_at=COALESCE(%s, qr_expires_at), "
                 "last_error=NULLIF(?,''), last_seen_at=CURRENT_TIMESTAMP, "
                 "updated_at=CURRENT_TIMESTAMP WHERE id=? RETURNING id",
                 qr_exp_cast);
        res = csilk_db_query_param_json(pool,
                                        sql,
                                        (const char*[]){status,
                                                        qr_content ? qr_content : "",
                                                        qr_expires_at ? qr_expires_at : "",
                                                        last_error ? last_error : "",
                                                        sid,
                                                        NULL});
    } else {
        /* PostgreSQL 的 qr_expires_at 为 TIMESTAMPTZ，文本参数必须显式 ::timestamptz 转型 */
        char sql[512];
        snprintf(sql,
                 sizeof(sql),
                 "UPDATE im_bot_instances SET status=?, qr_content=NULLIF(?,''), "
                 "qr_expires_at=%s, last_error=NULLIF(?,''), "
                 "last_seen_at=CURRENT_TIMESTAMP, updated_at=CURRENT_TIMESTAMP "
                 "WHERE id=? RETURNING id",
                 qr_exp_cast);
        res = csilk_db_query_param_json(pool,
                                        sql,
                                        (const char*[]){status,
                                                        qr_content ? qr_content : "",
                                                        qr_expires_at ? qr_expires_at : "",
                                                        last_error ? last_error : "",
                                                        sid,
                                                        NULL});
    }
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -3;
    }
    csilk_json_free(res);
    return 0;
}

int
mf_im_bot_instance_activate(csilk_db_pool_t* pool,
                            int64_t          id,
                            const char*      ilink_bot_id,
                            const char*      ilink_user_id,
                            char*            err_buf,
                            size_t           err_cap)
{
    if (err_buf && err_cap > 0) {
        err_buf[0] = 0;
    }
    if (!pool || id <= 0 || !ilink_bot_id || !ilink_bot_id[0] || !ilink_user_id ||
        !ilink_user_id[0]) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "invalid parameters");
        }
        return -1;
    }
    char sid[32], new_im[512], old_im[512];
    im_id_str(id, sid, sizeof(sid));
    snprintf(new_im, sizeof(new_im), "ilink:%s", ilink_user_id);
    old_im[0] = 0;

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_instance_activate", &scope) != 0) {
        return -1;
    }
    char sql[700];
    SQL_FMT(sql,
            sizeof(sql),
            "SELECT platform, mf_user_id, im_user_id, capability FROM im_bot_instances "
            "WHERE id=? LIMIT 1%s",
            db_is_postgres() ? " FOR UPDATE" : "");
    csilk_json_t* row = csilk_db_query_param_json(pool, sql, (const char*[]){sid, NULL});
    if (!row || !csilk_json_is_array(row) || csilk_json_array_size(row) == 0) {
        if (row) {
            csilk_json_free(row);
        }
        db_tx_scope_rollback(pool, &scope);
        return -3;
    }
    csilk_json_t* r0 = csilk_json_array_get(row, 0);
    char          plat[64] = "wechat";
    const char*   plat_ptr = csilk_json_get_string(r0, "platform");
    if (plat_ptr && plat_ptr[0]) {
        snprintf(plat, sizeof(plat), "%s", plat_ptr);
    }
    int64_t     owner = db_get_int(r0, "mf_user_id");
    const char* old_ptr = csilk_json_get_string(r0, "im_user_id");
    if (old_ptr && old_ptr[0]) {
        snprintf(old_im, sizeof(old_im), "%s", old_ptr);
    }
    char        cap_buf[32] = "readonly";
    const char* cap_ptr = csilk_json_get_string(r0, "capability");
    if (cap_ptr && cap_ptr[0]) {
        snprintf(cap_buf, sizeof(cap_buf), "%s", cap_ptr);
    }
    csilk_json_free(row);
    if (owner <= 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }

    csilk_json_t* conflict =
        csilk_db_query_param_json(pool,
                                  "SELECT id FROM im_bot_instances WHERE platform=? AND id<>? "
                                  "AND (im_user_id=? OR ilink_bot_id=?) LIMIT 1",
                                  (const char*[]){plat, sid, new_im, ilink_bot_id, NULL});
    if (conflict && csilk_json_is_array(conflict) && csilk_json_array_size(conflict) > 0) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "该微信账号已绑定其他实例");
        }
        csilk_json_free(conflict);
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    if (conflict) {
        csilk_json_free(conflict);
    }

    char    scope_buf[32] = {0};
    int64_t bound = mf_im_bot_repo_lookup(pool, plat, new_im, scope_buf, sizeof(scope_buf));
    if (bound > 0 && bound != owner) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "该微信账号已绑定其他用户");
        }
        db_tx_scope_rollback(pool, &scope);
        return -3;
    }
    if (old_im[0] && strcmp(old_im, new_im) != 0) {
        bound = mf_im_bot_repo_lookup(pool, plat, old_im, scope_buf, sizeof(scope_buf));
        if (bound > 0 && bound != owner) {
            if (err_buf && err_cap > 0) {
                snprintf(err_buf, err_cap, "原微信账号已绑定其他用户");
            }
            db_tx_scope_rollback(pool, &scope);
            return -3;
        }
    }

    if (mf_im_bot_repo_upsert(pool, plat, new_im, owner, cap_buf) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    bound = mf_im_bot_repo_lookup(pool, plat, new_im, scope_buf, sizeof(scope_buf));
    if (bound != owner) {
        if (err_buf && err_cap > 0) {
            snprintf(err_buf, err_cap, "绑定写入失败");
        }
        db_tx_scope_rollback(pool, &scope);
        return -3;
    }
    if (old_im[0] && strcmp(old_im, new_im) != 0) {
        char owner_str[32];
        im_id_str(owner, owner_str, sizeof(owner_str));
        csilk_db_exec_param(
            pool,
            "DELETE FROM im_bot_bindings WHERE platform=? AND im_user_id=? AND mf_user_id=?",
            (const char*[]){plat, old_im, owner_str, NULL});
    }

    bool        switched_wechat = (old_im[0] && strcmp(old_im, new_im) != 0);
    const char* activate_sql =
        switched_wechat
            ? "UPDATE im_bot_instances SET ilink_bot_id=?, ilink_user_id=?, im_user_id=?, "
              "status='active', enabled=1, qr_content=NULL, qr_expires_at=NULL, "
              "pending_verify_code=NULL, pending_verify_expires_at=NULL, verify_attempts=0, "
              "last_error=NULL, last_inbound_at=NULL, updated_at=CURRENT_TIMESTAMP "
              "WHERE id=? RETURNING id"
            : "UPDATE im_bot_instances SET ilink_bot_id=?, ilink_user_id=?, im_user_id=?, "
              "status='active', enabled=1, qr_content=NULL, qr_expires_at=NULL, "
              "pending_verify_code=NULL, pending_verify_expires_at=NULL, verify_attempts=0, "
              "last_error=NULL, updated_at=CURRENT_TIMESTAMP WHERE id=? RETURNING id";
    csilk_json_t* updated = csilk_db_query_param_json(
        pool, activate_sql, (const char*[]){ilink_bot_id, ilink_user_id, new_im, sid, NULL});
    int rc = 0;
    if (!updated || !csilk_json_is_array(updated) || csilk_json_array_size(updated) == 0) {
        rc = -1;
    }
    if (updated) {
        csilk_json_free(updated);
    }
    if (rc == 0 && db_tx_scope_commit(pool, &scope) != 0) {
        return -1;
    }
    if (rc != 0) {
        db_tx_scope_rollback(pool, &scope);
    }
    return rc;
}

csilk_json_t*
mf_im_bot_instance_get_by_user(csilk_db_pool_t* pool,
                               int64_t          user_id,
                               const char*      platform,
                               const char*      adapter)
{
    if (!pool || user_id <= 0) {
        return NULL;
    }
    const char* plat = (platform && platform[0]) ? platform : "wechat";
    const char* adp = (adapter && adapter[0]) ? adapter : "ilink";
    char        uid[32];
    im_id_str(user_id, uid, sizeof(uid));
    return im_instance_one(pool,
                           "mf_user_id=? AND platform=? AND adapter=? LIMIT 1",
                           (const char*[]){uid, plat, adp, NULL});
}

static int
im_parse_2d(const char* p, int min_v, int max_v)
{
    if (!p || !isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) {
        return -1;
    }
    int v = (p[0] - '0') * 10 + (p[1] - '0');
    return (v >= min_v && v <= max_v) ? v : -1;
}

static int
im_valid_iso_prefix(const char* ts)
{
    if (!ts) {
        return 0;
    }
    for (int i = 0; i < 4; ++i) {
        if (!isdigit((unsigned char)ts[i])) {
            return 0;
        }
    }
    if (ts[4] != '-' || im_parse_2d(ts + 5, 1, 12) < 0 || ts[7] != '-' ||
        im_parse_2d(ts + 8, 1, 31) < 0) {
        return 0;
    }
    if (ts[10] != 'T' && ts[10] != ' ') {
        return 0;
    }
    if (im_parse_2d(ts + 11, 0, 23) < 0 || ts[13] != ':' || im_parse_2d(ts + 14, 0, 59) < 0 ||
        ts[16] != ':' || im_parse_2d(ts + 17, 0, 59) < 0) {
        return 0;
    }
    const char* p = ts + 19;
    if (*p == '.') {
        ++p;
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
    }
    if (*p == 'Z') {
        ++p;
    } else if (*p == '+' || *p == '-') {
        ++p;
        if (im_parse_2d(p, 0, 23) < 0) {
            return 0;
        }
        p += 2;
        if (*p == ':') {
            ++p;
            if (im_parse_2d(p, 0, 59) < 0) {
                return 0;
            }
            p += 2;
        } else if (isdigit((unsigned char)*p)) {
            if (im_parse_2d(p, 0, 59) < 0) {
                return 0;
            }
            p += 2;
        }
    }
    return *p == '\0';
}

static const char*
im_inbound_expr(csilk_db_pool_t* pool)
{
    const char* drv = csilk_db_pool_get_driver_name(pool);
    const bool  is_pg = drv && strcmp(drv, "postgres") == 0;
    return is_pg ? "COALESCE(NULLIF(?,'')::timestamptz, last_inbound_at)"
                 : "COALESCE(NULLIF(?,''), last_inbound_at)";
}

int
mf_im_bot_instance_touch_inbound(csilk_db_pool_t* pool, int64_t id, const char* last_inbound_at)
{
    if (!pool || id <= 0) {
        return -1;
    }
    char sid[32];
    im_id_str(id, sid, sizeof(sid));
    if (!im_valid_iso_prefix(last_inbound_at)) {
        return csilk_db_exec_param(pool,
                                   "UPDATE im_bot_instances SET last_seen_at=CURRENT_TIMESTAMP, "
                                   "updated_at=CURRENT_TIMESTAMP WHERE id=?",
                                   (const char*[]){sid, NULL});
    }
    char sql[512];
    SQL_FMT(sql,
            sizeof(sql),
            "UPDATE im_bot_instances SET last_inbound_at=%s, "
            "last_seen_at=CURRENT_TIMESTAMP, updated_at=CURRENT_TIMESTAMP WHERE id=?",
            im_inbound_expr(pool));
    return csilk_db_exec_param(pool, sql, (const char*[]){last_inbound_at, sid, NULL});
}

int
mf_im_bot_instance_heartbeat_with_inbound(csilk_db_pool_t* pool,
                                          int64_t          id,
                                          const char*      last_inbound_at,
                                          char*            status_out,
                                          size_t           status_cap,
                                          int64_t*         epoch_out,
                                          int*             enabled_out)
{
    if (!pool || id <= 0) {
        return -1;
    }
    char sid[32];
    im_id_str(id, sid, sizeof(sid));
    const char* inb = im_valid_iso_prefix(last_inbound_at) ? last_inbound_at : "";
    char        sql[512];
    SQL_FMT(sql,
            sizeof(sql),
            "UPDATE im_bot_instances SET last_inbound_at=%s, "
            "last_seen_at=CURRENT_TIMESTAMP, updated_at=CURRENT_TIMESTAMP "
            "WHERE id=? RETURNING status, login_epoch, enabled",
            im_inbound_expr(pool));
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){inb, sid, NULL});
    if (!res) {
        return -1;
    }
    if (!csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        csilk_json_free(res);
        return -3;
    }
    csilk_json_t* row = csilk_json_array_get(res, 0);
    if (status_out && status_cap > 0) {
        const char* st = csilk_json_get_string(row, "status");
        snprintf(status_out, status_cap, "%s", st ? st : "");
    }
    if (epoch_out) {
        *epoch_out = db_get_int(row, "login_epoch");
    }
    if (enabled_out) {
        *enabled_out = db_get_bool(row, "enabled") ? 1 : 0;
    }
    csilk_json_free(res);
    return 0;
}

int
mf_im_bot_instance_heartbeat(csilk_db_pool_t* pool,
                             int64_t          id,
                             char*            status_out,
                             size_t           status_cap,
                             int64_t*         epoch_out,
                             int*             enabled_out)
{
    return mf_im_bot_instance_heartbeat_with_inbound(
        pool, id, NULL, status_out, status_cap, epoch_out, enabled_out);
}

int
mf_im_bot_instance_set_capability(csilk_db_pool_t* pool,
                                  int64_t          user_id,
                                  int64_t          id,
                                  const char*      capability)
{
    if (!pool || user_id <= 0 || id <= 0 || !capability || !capability[0]) {
        return -1;
    }
    char id_str[32], uid_str[32];
    im_id_str(id, id_str, sizeof(id_str));
    im_id_str(user_id, uid_str, sizeof(uid_str));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_instance_set_capability", &scope) != 0) {
        return -1;
    }
    char sql[512];
    SQL_FMT(sql,
            sizeof(sql),
            "SELECT platform, im_user_id FROM im_bot_instances "
            "WHERE id=? AND mf_user_id=? LIMIT 1%s",
            db_is_postgres() ? " FOR UPDATE" : "");
    csilk_json_t* row =
        csilk_db_query_param_json(pool, sql, (const char*[]){id_str, uid_str, NULL});
    if (!row || !csilk_json_is_array(row) || csilk_json_array_size(row) == 0) {
        if (row) {
            csilk_json_free(row);
        }
        db_tx_scope_rollback(pool, &scope);
        return -3; /* not found / not owner */
    }
    csilk_json_t* r0 = csilk_json_array_get(row, 0);
    char          plat[64] = "wechat";
    const char*   plat_ptr = csilk_json_get_string(r0, "platform");
    if (plat_ptr && plat_ptr[0]) {
        snprintf(plat, sizeof(plat), "%s", plat_ptr);
    }
    const char* imu_ptr = csilk_json_get_string(r0, "im_user_id");
    char        im_uid[256] = "";
    if (imu_ptr && imu_ptr[0]) {
        snprintf(im_uid, sizeof(im_uid), "%s", imu_ptr);
    }
    csilk_json_free(row);

    if (csilk_db_exec_param(
            pool,
            "UPDATE im_bot_instances SET capability=?, updated_at=CURRENT_TIMESTAMP "
            "WHERE id=? AND mf_user_id=?",
            (const char*[]){capability, id_str, uid_str, NULL}) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (im_uid[0] &&
        csilk_db_exec_param(
            pool,
            "UPDATE im_bot_bindings SET scope=? WHERE platform=? AND im_user_id=? AND mf_user_id=?",
            (const char*[]){capability, plat, im_uid, uid_str, NULL}) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    db_tx_scope_commit(pool, &scope);
    return 0;
}

int
mf_im_bot_pending_action_insert(csilk_db_pool_t* pool,
                                const char*      code,
                                int64_t          user_id,
                                int64_t          session_id,
                                const char*      action_type,
                                const char*      draft_token,
                                const char*      args_json,
                                int64_t          expires_at)
{
    if (!pool || !code || !code[0] || !draft_token || !draft_token[0] || !args_json ||
        !args_json[0] || user_id <= 0 || session_id <= 0) {
        return -1;
    }
    char uid[32], sid[32], exp[32];
    im_id_str(user_id, uid, sizeof(uid));
    im_id_str(session_id, sid, sizeof(sid));
    im_id_str(expires_at, exp, sizeof(exp));
    const char* at = action_type && action_type[0] ? action_type : "daily_expense";
    return csilk_db_exec_param(
        pool,
        "INSERT INTO im_pending_actions "
        "(code, user_id, session_id, action_type, draft_token, args_json, expires_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        (const char*[]){code, uid, sid, at, draft_token, args_json, exp, NULL});
}

csilk_json_t*
mf_im_bot_pending_action_get(
    csilk_db_pool_t* pool, const char* code, int64_t user_id, int64_t session_id, int64_t now)
{
    if (!pool || !code || !code[0] || user_id <= 0 || session_id <= 0) {
        return NULL;
    }
    char uid[32], sid[32], now_s[32];
    im_id_str(user_id, uid, sizeof(uid));
    im_id_str(session_id, sid, sizeof(sid));
    im_id_str(now, now_s, sizeof(now_s));
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT code, user_id, session_id, action_type, draft_token, args_json, expires_at "
        "FROM im_pending_actions "
        "WHERE code=? AND user_id=? AND session_id=? AND consumed_at IS NULL AND expires_at>? "
        "LIMIT 1",
        (const char*[]){code, uid, sid, now_s, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return NULL;
    }
    csilk_json_t* copy = csilk_json_copy(csilk_json_array_get(res, 0));
    csilk_json_free(res);
    return copy;
}

int
mf_im_bot_pending_action_consume(csilk_db_pool_t* pool,
                                 const char*      code,
                                 int64_t          user_id,
                                 int64_t          session_id)
{
    if (!pool || !code || !code[0] || user_id <= 0 || session_id <= 0) {
        return -1;
    }
    char uid[32], sid[32], now_s[32];
    im_id_str(user_id, uid, sizeof(uid));
    im_id_str(session_id, sid, sizeof(sid));
    im_id_str((int64_t)time(NULL), now_s, sizeof(now_s));
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE im_pending_actions SET consumed_at=? "
        "WHERE code=? AND user_id=? AND session_id=? AND consumed_at IS NULL RETURNING code",
        (const char*[]){now_s, code, uid, sid, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }
    csilk_json_free(res);
    return 0;
}
