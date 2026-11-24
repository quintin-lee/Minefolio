/**
 * @file admin_controller.c
 * @brief 系统管理与初始引导控制器实现 (DDD 接口层)
 */

#include "interfaces/http/controllers/admin_controller.h"
#include "infrastructure/repositories/auth_repo_impl.h"
#include "infrastructure/repositories/import_rule_repo_impl.h"
#include "infrastructure/database/db_backup.h"
#define MINEFOLIO_BCRYPT_COST CSILK_BCRYPT_DEFAULT_COST
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/jwt.h"
#include "config/key_manager.h"
#include "application/category/usecases.h"
#include "application/account/usecases.h"
#include "application/skill/usecases.h"
#include "application/task/async_task_manager.h"
#include "csilk/csilk.h"
#include "csilk/crypto/bcrypt.h"
#include "csilk/core/primitives/codec.h"
#include "csilk/crypto/crypto_dispatch.h"
#include "csilk/drivers/cipher.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef MINEFOLIO_VERSION
#define MINEFOLIO_VERSION "1.4.0"
#endif

static void
store_bcrypt_hash(const char* password, char* out)
{
    csilk_bcrypt_hash(password, strlen(password), MINEFOLIO_BCRYPT_COST, out);
}

void
system_status(csilk_ctx_t* c)
{
    csilk_db_pool_t* pool = db_get_pool();
    int              count = mf_auth_repo_count(pool);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_bool(resp, "initialized", count > 0);
    csilk_json_add_string(resp, "version", MINEFOLIO_VERSION);
    /* user_count intentionally omitted: leaking the number of registered
     * users assists brute-force and social-engineering attacks. */
    respond_ok(c, resp);
}

void
system_setup(csilk_ctx_t* c)
{
    csilk_db_pool_t* pool = db_get_pool();

    // Verify system is not initialized yet
    if (mf_auth_repo_count(pool) > 0) {
        respond_forbidden(c, "系统已完成初始化，禁止重复设置");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char* username = csilk_json_get_string(body, "username");
    const char* password_enc = csilk_json_get_string(body, "password_enc");
    if (!username || !password_enc || strlen(username) < 2) {
        csilk_json_free(body);
        respond_bad_request(c, "用户名需≥2字符");
        return;
    }

    /* Decrypt the password */
    uint8_t pt_buf[512];
    size_t  pt_len = sizeof(pt_buf);
    uint8_t ct_buf[CSILK_RSA_KEY_SIZE];
    if (csilk_base64url_decode(password_enc, ct_buf, sizeof(ct_buf)) < 0) {
        csilk_json_free(body);
        respond_bad_request(c, "密码格式错误");
        return;
    }
    if (_csilk_asymmetric_decrypt(c,
                                  auth_key_get_private_pem(),
                                  strlen(auth_key_get_private_pem()),
                                  ct_buf,
                                  CSILK_RSA_KEY_SIZE,
                                  pt_buf,
                                  &pt_len) != 0 ||
        pt_len == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "密码解密失败");
        return;
    }
    if (pt_len < 6) {
        csilk_json_free(body);
        respond_bad_request(c, "密码需≥6字符");
        return;
    }
    pt_buf[pt_len] = '\0';
    const char* password = (const char*)pt_buf;

    // Start transaction
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_setup_scope", &scope) != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    char hashed[CSILK_BCRYPT_HASH_LEN];
    store_bcrypt_hash(password, hashed);

    int64_t user_id = 0;
    if (mf_auth_repo_create(pool, username, hashed, &user_id) != 0 || user_id <= 0) {
        db_tx_scope_rollback(pool, &scope);
        csilk_json_free(body);
        respond_error(c, 500, "初始化失败");
        return;
    }

    // Seed default ledger first, then ledger-scoped categories & import rules
    int64_t ledger_id = ledger_get_default(pool, user_id);
    category_usecase_seed_defaults(pool, user_id, ledger_id);
    account_usecase_seed_default(pool, user_id, ledger_id);
    mf_import_rule_seed_defaults(pool, user_id);
    skill_usecase_seed_defaults(pool, user_id);

    /*
     * DB 配置完全由环境变量 (MINEFOLIO_DB_DRIVER / MINEFOLIO_DB_DSN) 提供，
     * 不再通过 setup 接口写入 config/db.json (避免容器部署时残留文件与 env
     * 优先级冲突导致误判未初始化)。前端提交的 db_driver / db_dsn 字段被忽略。
     */

    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        csilk_json_free(body);
        respond_error(c, 500, "提交事务失败");
        return;
    }
    csilk_json_free(body);

    char*         token = jwt_generate_token(c, user_id, 0); /* new admin, version 0 */
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);

    free(token);
    respond_ok(c, resp);
}

void
system_backup_status(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    status = NULL;
    if (db_backup_get_status(pool, "data/backups", &status) != 0 || !status) {
        respond_error(c, 1005, "获取备份存储状态失败");
        return;
    }
    respond_ok(c, status);
}

static int
system_backup_create_task_fn(void*   pool,
                             int64_t task_id,
                             int64_t user_id,
                             int64_t ledger_id,
                             void*   arg,
                             char**  out_result_json,
                             char*   err_buf,
                             size_t  err_cap)
{
    (void)user_id;
    (void)ledger_id;
    (void)arg;
    mf_async_task_update_progress(pool, task_id, 20, "正在执行 SQLite 在线热备份");

    char filename[128] = {0};
    if (db_backup_create((csilk_db_pool_t*)pool, "data/backups", filename, sizeof(filename)) != 0) {
        snprintf(err_buf, err_cap, "执行数据库在线热备份失败");
        return -1;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "filename", filename);
    if (out_result_json) {
        *out_result_json = csilk_json_serialize(resp, NULL);
    }
    csilk_json_free(resp);
    return 0;
}

void
system_backup_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    if (db_is_postgres()) {
        respond_error(c, 1005, "执行数据库在线热备份失败");
        return;
    }

    bool    deduplicated = false;
    int64_t task_id = mf_async_task_submit(db_get_pool(),
                                           user_id,
                                           0,
                                           "system_backup_create",
                                           "system:backup:create",
                                           "正在排队创建数据库在线热备份",
                                           system_backup_create_task_fn,
                                           NULL,
                                           NULL,
                                           &deduplicated);
    if (task_id <= 0) {
        respond_error(c, 1005, "执行数据库在线热备份失败");
        return;
    }

    mf_async_task_respond_submitted(c, task_id, "system_backup_create", deduplicated, NULL);
}

void
system_backup_download(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    const char* filename = csilk_get_param(c, "filename");
    if (!filename || !db_backup_is_valid_filename(filename)) {
        respond_error(c, 1002, "非法文件名或检测到路径穿越");
        return;
    }

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "data/backups/%s", filename);

    FILE* fp = fopen(full_path, "rb");
    if (!fp) {
        respond_error(c, 1002, "备份文件不存在");
        return;
    }

    char disp[256];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", filename);

    csilk_status(c, CSILK_STATUS_OK);
    csilk_set_header(c, "Content-Type", "application/x-sqlite3");
    csilk_set_header(c, "Content-Disposition", disp);

    uint8_t buf[16384];
    size_t  n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        csilk_response_write(c, buf, n);
    }
    fclose(fp);
    csilk_response_end(c);
}

void
system_backup_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    const char* filename = csilk_get_param(c, "filename");
    if (!filename || !db_backup_is_valid_filename(filename)) {
        respond_error(c, 1002, "非法文件名或检测到路径穿越");
        return;
    }

    if (db_backup_delete("data/backups", filename) != 0) {
        respond_error(c, 1002, "备份文件不存在或删除失败");
        return;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_bool(resp, "success", true);
    respond_ok(c, resp);
}

void
register_admin_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/system/status",
                      system_status,
                      nullptr,
                      nullptr,
                      "System status",
                      "Returns initialization status and user count");
    csilk_app_post_ext(app,
                       "/api/system/setup",
                       system_setup,
                       "setup_req_t",
                       "token_resp_t",
                       "Initialize system",
                       "Seed the database with default categories for the first admin user");
    csilk_app_get(app, "/api/system/backup/status", system_backup_status);
    csilk_app_post(app, "/api/system/backup/create", system_backup_create);
    csilk_app_get(app, "/api/system/backup/download/:filename", system_backup_download);
    csilk_app_delete(app, "/api/system/backup/:filename", system_backup_delete);
}
