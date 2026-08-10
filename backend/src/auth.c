#include "common/response.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/csilk.h"
#include "csilk/core/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** @brief HMAC-SHA256 password hash with pepper from env. */
static void hash_password(const char* password, char* out_hash, size_t out_len) {
    const char* pepper = getenv("MINEFOLIO_JWT_SECRET");
    if (!pepper) pepper = "minefolio-dev-secret";

    csilk_sha256_ctx ctx;
    csilk_sha256_init(&ctx);
    csilk_sha256_update(&ctx, (const uint8_t*)pepper, strlen(pepper));
    csilk_sha256_update(&ctx, (const uint8_t*)password, strlen(password));
    uint8_t digest[32];
    csilk_sha256_final(&ctx, digest);

    for (size_t i = 0; i < 32 && (i * 2 + 2) < out_len; i++)
        sprintf(out_hash + i * 2, "%02x", digest[i]);
}

/** @brief POST /api/auth/register — 注册（仅首次用户）*/
void auth_register(csilk_ctx_t* c) {
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* username = csilk_json_get_string(body, "username");
    const char* password = csilk_json_get_string(body, "password");
    if (!username || !password || strlen(username) < 2 || strlen(password) < 4) {
        csilk_json_free(body);
        respond_bad_request(c, "用户名需≥2字符，密码需≥4字符");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();

    // Check if user already exists
    const char* check_sql = "SELECT id FROM users WHERE username = ?";
    const char* check_params[] = { username, NULL };
    csilk_json_t* check = csilk_db_query_param_json(pool, check_sql, check_params);
    if (check) {
        csilk_json_free(check);
        csilk_json_free(body);
        respond_conflict(c, "用户名已存在");
        return;
    }

    // Hash password
    char hashed[65];
    hash_password(password, hashed, sizeof(hashed));

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

    int64_t user_id = (int64_t)csilk_json_get_number(csilk_json_array_get(user, 0), "id");
    char* token = jwt_generate_token(c, user_id);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);
    free(token);

    respond_ok(c, resp);
    csilk_json_free(user);
}

/** @brief POST /api/auth/login — 登录 */
void auth_login(csilk_ctx_t* c) {
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* username = csilk_json_get_string(body, "username");
    const char* password = csilk_json_get_string(body, "password");
    if (!username || !password) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少用户名或密码");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    char hashed[65];
    hash_password(password, hashed, sizeof(hashed));

    const char* sql = "SELECT id, username, created_at FROM users WHERE username = ? AND password = ?";
    const char* params[] = { username, hashed, NULL };
    csilk_json_t* result = csilk_db_query_param_json(pool, sql, params);
    csilk_json_free(body);

    if (!result || csilk_json_array_size(result) == 0) {
        csilk_json_free(result);
        respond_unauthorized(c);
        return;
    }

    int64_t user_id = (int64_t)csilk_json_get_number(csilk_json_array_get(result, 0), "id");
    char* token = jwt_generate_token(c, user_id);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);
    free(token);

    respond_ok(c, resp);
    csilk_json_free(result);
}

/** @brief GET /api/auth/me — 获取当前用户信息 */
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
    csilk_json_add_number(resp, "id", csilk_json_get_number(user, "id"));
    csilk_json_add_string(resp, "username", csilk_json_get_string(user, "username"));
    csilk_json_add_string(resp, "created_at", csilk_json_get_string(user, "created_at"));
    csilk_json_free(result);

    respond_ok(c, resp);
}
