#include "services/auth_service.h"
#define MINEFOLIO_BCRYPT_COST CSILK_BCRYPT_DEFAULT_COST
#include "common/response.h"
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
#include <string.h>

static void store_bcrypt_hash(const char* password, char* out) {
    csilk_bcrypt_hash(password, strlen(password), MINEFOLIO_BCRYPT_COST, out);
}

void system_status(csilk_ctx_t* c) {
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* res = csilk_db_query_json(pool, "SELECT COUNT(*) as count FROM users");
    int count = 0;
    if (res && csilk_json_array_size(res) > 0) {
        count = (int)db_get_int(csilk_json_array_get(res, 0), "count");
    }
    if (res) csilk_json_free(res);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_bool(resp, "initialized", count > 0);
    csilk_json_add_number(resp, "user_count", count);
    respond_ok(c, resp);
}

void system_setup(csilk_ctx_t* c) {
    csilk_db_pool_t* pool = db_get_pool();

    // Verify system is not initialized yet
    csilk_json_t* count_res = csilk_db_query_json(pool, "SELECT COUNT(*) as count FROM users");
    int count = 0;
    if (count_res && csilk_json_array_size(count_res) > 0) {
        count = (int)db_get_int(csilk_json_array_get(count_res, 0), "count");
    }
    if (count_res) csilk_json_free(count_res);

    if (count > 0) {
        respond_forbidden(c, "系统已完成初始化，禁止重复设置");
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

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
            auth_key_get_private_pem(), strlen(auth_key_get_private_pem()),
            ct_buf, CSILK_RSA_KEY_SIZE, pt_buf, &pt_len) != 0 || pt_len == 0) {
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

    const char* insert_sql = "INSERT INTO users (username, password) VALUES (?, ?)";
    const char* insert_params[] = { username, hashed, NULL };
    csilk_json_t* ins_res = csilk_db_query_param_json(pool, insert_sql, insert_params);
    if (ins_res) csilk_json_free(ins_res);

    const char* get_params[] = { username, NULL };
    csilk_json_t* user_res = csilk_db_query_param_json(pool,
        "SELECT id, username, password FROM users WHERE username = ?", get_params);
    if (!user_res || csilk_json_array_size(user_res) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        if (user_res) csilk_json_free(user_res);
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
    const char* db_dsn    = csilk_json_get_string(body, "db_dsn");
    if (db_driver) {
        const char* kv[] = { "driver", db_driver, "dsn", db_dsn ? db_dsn : "", NULL };
        config_set("config/db.json", kv);
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);

    char* token = jwt_generate_token(c, user_id);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);

    free(token);
    respond_ok(c, resp);
}

void auth_register(csilk_ctx_t* c) {
    csilk_db_pool_t* pool = db_get_pool();

    // Check if system is already initialized
    csilk_json_t* count_res = csilk_db_query_json(pool, "SELECT COUNT(*) as count FROM users");
    if (count_res && csilk_json_array_size(count_res) > 0) {
        int cnt = (int)db_get_int(csilk_json_array_get(count_res, 0), "count");
        csilk_json_free(count_res);
        if (cnt > 0) {
            respond_conflict(c, "系统已完成初始化，禁止公开注册");
            return;
        }
    } else if (count_res) {
        csilk_json_free(count_res);
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* username = csilk_json_get_string(body, "username");
    const char* password = csilk_json_get_string(body, "password");
    if (!username || !password || strlen(username) < 2 || strlen(password) < 4) {
        csilk_json_free(body);
        respond_bad_request(c, "用户名需≥2字符，密码需≥4字符");
        return;
    }

    // Check if user already exists
    const char* check_sql = "SELECT id FROM users WHERE username = ?";
    const char* check_params[] = { username, NULL };
    csilk_json_t* check = csilk_db_query_param_json(pool, check_sql, check_params);
    if (check && csilk_json_array_size(check) > 0) {
        csilk_json_free(check);
        csilk_json_free(body);
        respond_conflict(c, "用户名已存在");
        return;
    }
    if (check) csilk_json_free(check);

    // Hash password
    char hashed[CSILK_BCRYPT_HASH_LEN];
    store_bcrypt_hash(password, hashed);

    // Insert user
    const char* insert_sql = "INSERT INTO users (username, password) VALUES (?, ?)";
    const char* insert_params[] = { username, hashed, NULL };
    csilk_db_query_param_json(pool, insert_sql, insert_params);

    // Get user id
    csilk_json_t* user = csilk_db_query_param_json(pool,
        "SELECT id, username, created_at FROM users WHERE username = ?",
        (const char*[]){username, NULL});

    csilk_json_free(body);
    if (!user || csilk_json_array_size(user) == 0) {
        respond_error(c, 500, "注册失败");
        if (user) csilk_json_free(user);
        return;
    }

    int64_t user_id = db_get_int(csilk_json_array_get(user, 0), "id");
    char* token = jwt_generate_token(c, user_id);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);
    free(token);

    respond_ok(c, resp);
    csilk_json_free(user);
}

void auth_login(csilk_ctx_t* c) {
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* username = csilk_json_get_string(body, "username");
    const char* password_enc = csilk_json_get_string(body, "password_enc");
    if (!username || !password_enc) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少用户名或密码");
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
            auth_key_get_private_pem(), strlen(auth_key_get_private_pem()),
            ct_buf, CSILK_RSA_KEY_SIZE, pt_buf, &pt_len) != 0 || pt_len == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "密码解密失败");
        return;
    }
    pt_buf[pt_len] = '\0';
    const char* password = (const char*)pt_buf;

    csilk_db_pool_t* pool = db_get_pool();

    const char* sql = "SELECT id, username, password FROM users WHERE username = ?";
    const char* params[] = { username, NULL };
    csilk_json_t* result = csilk_db_query_param_json(pool, sql, params);
    csilk_json_free(body);

    if (!result || csilk_json_array_size(result) == 0) {
        csilk_json_free(result);
        respond_unauthorized(c);
        return;
    }

    csilk_json_t* row = csilk_json_array_get(result, 0);
    const char* stored_hash = csilk_json_get_string(row, "password");
    if (!stored_hash || csilk_bcrypt_verify(password, pt_len, stored_hash) != 0) {
        csilk_json_free(result);
        respond_unauthorized(c);
        return;
    }

    int64_t user_id = db_get_int(row, "id");
    char* token = jwt_generate_token(c, user_id);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);
    free(token);

    respond_ok(c, resp);
    csilk_json_free(result);
}

void auth_change_password(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* old_password_enc = csilk_json_get_string(body, "old_password_enc");
    const char* new_password_enc = csilk_json_get_string(body, "new_password_enc");
    if (!old_password_enc || !new_password_enc) {
        csilk_json_free(body);
        respond_bad_request(c, "原密码和新密码不能为空");
        return;
    }

    /* Decrypt old password */
    uint8_t old_pt[512], new_pt[512];
    size_t  old_pt_len = sizeof(old_pt), new_pt_len = sizeof(new_pt);
    uint8_t ct[CSILK_RSA_KEY_SIZE];

    if (csilk_base64url_decode(old_password_enc, ct, sizeof(ct)) < 0) {
        csilk_json_free(body);
        respond_bad_request(c, "密码格式错误");
        return;
    }
    if (_csilk_asymmetric_decrypt(c,
            auth_key_get_private_pem(), strlen(auth_key_get_private_pem()),
            ct, CSILK_RSA_KEY_SIZE, old_pt, &old_pt_len) != 0 || old_pt_len == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "密码解密失败");
        return;
    }
    old_pt[old_pt_len] = '\0';

    if (csilk_base64url_decode(new_password_enc, ct, sizeof(ct)) < 0) {
        csilk_json_free(body);
        respond_bad_request(c, "密码格式错误");
        return;
    }
    if (_csilk_asymmetric_decrypt(c,
            auth_key_get_private_pem(), strlen(auth_key_get_private_pem()),
            ct, CSILK_RSA_KEY_SIZE, new_pt, &new_pt_len) != 0 || new_pt_len == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "密码解密失败");
        return;
    }
    new_pt[new_pt_len] = '\0';
    if (new_pt_len < 6) {
        csilk_json_free(body);
        respond_bad_request(c, "新密码需≥6字符");
        return;
    }

    if (strcmp((const char*)old_pt, (const char*)new_pt) == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "新密码不能与原密码相同");
        return;
    }

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_db_pool_t* pool = db_get_pool();
    const char* sql = "SELECT password FROM users WHERE id = ?";
    const char* check_params[] = { uid_str, NULL };
    csilk_json_t* check = csilk_db_query_param_json(pool, sql, check_params);
    if (!check || csilk_json_array_size(check) == 0) {
        if (check) csilk_json_free(check);
        csilk_json_free(body);
        respond_bad_request(c, "原密码不正确");
        return;
    }
    const char* stored_hash = csilk_json_get_string(csilk_json_array_get(check, 0), "password");
    csilk_json_free(check);
    if (!stored_hash || csilk_bcrypt_verify((const char*)old_pt, old_pt_len, stored_hash) != 0) {
        csilk_json_free(body);
        respond_bad_request(c, "原密码不正确");
        return;
    }

    char new_hashed[CSILK_BCRYPT_HASH_LEN];
    store_bcrypt_hash((const char*)new_pt, new_hashed);

    const char* update_params[] = { new_hashed, uid_str, NULL };
    csilk_json_t* update_res = csilk_db_query_param_json(pool,
        "UPDATE users SET password = ? WHERE id = ?", update_params);
    if (update_res) csilk_json_free(update_res);

    csilk_json_free(body);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "code", 0);
    csilk_json_add_string(resp, "message", "密码修改成功");
    respond_ok(c, resp);
}

void auth_me(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_db_pool_t* pool = db_get_pool();
    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT id, username, created_at FROM users WHERE id = %lld", (long long)user_id);

    csilk_json_t* result = csilk_db_query_json(pool, sql);
    if (!result || csilk_json_array_size(result) == 0) {
        respond_not_found(c);
        if (result) csilk_json_free(result);
        return;
    }

    csilk_json_t* user = csilk_json_array_get(result, 0);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", db_get_num(user, "id"));
    csilk_json_add_string(resp, "username", csilk_json_get_string(user, "username"));
    csilk_json_add_string(resp, "created_at", csilk_json_get_string(user, "created_at"));
    csilk_json_free(result);

    respond_ok(c, resp);
}
void register_auth_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/system/status", system_status, nullptr, nullptr, "System status", "Returns initialization status and user count");
    csilk_app_post_ext(app, "/api/system/setup", system_setup, "setup_req_t", "token_resp_t", "Initialize system", "Seed the database with default categories for the first admin user");
    csilk_app_post_ext(app, "/api/auth/register", auth_register, "register_req_t", "token_resp_t", "Register admin", "Create the first admin user (only allowed before system initialization)");
    csilk_app_post_ext(app, "/api/auth/login", auth_login, "login_req_t", "token_resp_t", "Login", "Authenticate with username and RSA-encrypted password, returns JWT token");
    csilk_app_get_ext(app, "/api/auth/public-key", auth_public_key, nullptr, nullptr, "Get public key", "Returns the RSA public key PEM for client-side password encryption");
    csilk_app_get_ext(app, "/api/auth/me", auth_me, nullptr, "user_resp_t", "Get current user profile", "Returns the authenticated user's profile");
    csilk_app_put_ext(app, "/api/auth/password", auth_change_password, "change_pwd_req_t", nullptr, "Change password", "Update the current user's password using encrypted old/new values");
}
