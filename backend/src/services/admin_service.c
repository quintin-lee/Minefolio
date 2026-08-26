#include "services/admin_service.h"
#include "repositories/auth_repo.h"
#define MINEFOLIO_BCRYPT_COST CSILK_BCRYPT_DEFAULT_COST
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/config.h"
#include "common/jwt.h"
#include "config/key_manager.h"
#include "controllers/category_controller.h"
#include "csilk/csilk.h"
#include "csilk/core/bcrypt.h"
#include "csilk/core/codec.h"
#include "csilk/core/crypto_dispatch.h"
#include "csilk/drivers/cipher.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void
store_bcrypt_hash(const char* password, char* out)
{
    csilk_bcrypt_hash(password, strlen(password), MINEFOLIO_BCRYPT_COST, out);
}
void
system_status(csilk_ctx_t* c)
{
    csilk_db_pool_t* pool = db_get_pool();
    int              count = user_count(pool);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_bool(resp, "initialized", count > 0);
    /* user_count intentionally omitted: leaking the number of registered
     * users assists brute-force and social-engineering attacks. */
    respond_ok(c, resp);
}
void
system_setup(csilk_ctx_t* c)
{
    csilk_db_pool_t* pool = db_get_pool();

    // Verify system is not initialized yet
    csilk_json_t* count_res = csilk_db_query_json(pool, "SELECT COUNT(*) as count FROM users");
    int           count = 0;
    if (count_res && csilk_json_array_size(count_res) > 0) {
        count = (int)db_get_int(csilk_json_array_get(count_res, 0), "count");
    }
    if (count_res) {
        csilk_json_free(count_res);
    }

    if (count > 0) {
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
    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    char hashed[CSILK_BCRYPT_HASH_LEN];
    store_bcrypt_hash(password, hashed);

    const char*   insert_sql = "INSERT INTO users (username, password) VALUES (?, ?)";
    const char*   insert_params[] = {username, hashed, NULL};
    csilk_json_t* ins_res = csilk_db_query_param_json(pool, insert_sql, insert_params);
    if (ins_res) {
        csilk_json_free(ins_res);
    }

    const char*   get_params[] = {username, NULL};
    csilk_json_t* user_res = csilk_db_query_param_json(
        pool, "SELECT id, username, password FROM users WHERE username = ?", get_params);
    if (!user_res || csilk_json_array_size(user_res) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        if (user_res) {
            csilk_json_free(user_res);
        }
        csilk_json_free(body);
        respond_error(c, 500, "初始化失败");
        return;
    }

    int64_t user_id = db_get_int(csilk_json_array_get(user_res, 0), "id");
    csilk_json_free(user_res);

    // Seed default categories
    categories_seed_defaults(pool, user_id);

    /* Persist DB config if provided */
    const char* db_driver = csilk_json_get_string(body, "db_driver");
    const char* db_dsn = csilk_json_get_string(body, "db_dsn");
    if (db_driver) {
        const char* kv[] = {"driver", db_driver, "dsn", db_dsn ? db_dsn : "", NULL};
        config_set("config/db.json", kv);
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);

    char*         token = jwt_generate_token(c, user_id, 0); /* new admin, version 0 */
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);

    free(token);
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
}
