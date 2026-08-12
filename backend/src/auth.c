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

/** @brief GET /api/system/status — 查询系统初始化状态 */
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

/** @brief Helper to seed default category templates for new admin user */
static void seed_default_categories(csilk_db_pool_t* pool, int64_t user_id) {
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    // Income categories
    const char* income_cats[] = { "工资", "理财收益", "兼职/副业", "其他收入", NULL };
    for (int i = 0; income_cats[i]; i++) {
        const char* params[] = { uid_str, income_cats[i], "income", NULL };
        csilk_json_t* r = csilk_db_query_param_json(pool,
            "INSERT OR IGNORE INTO categories (user_id, name, type) VALUES (?, ?, ?)", params);
        if (r) csilk_json_free(r);
    }

    // Expense categories
    const char* expense_cats[] = { "餐饮", "交通", "居住", "购物", "娱乐", "医疗", "数码电子", "其他支出", NULL };
    for (int i = 0; expense_cats[i]; i++) {
        const char* params[] = { uid_str, expense_cats[i], "expense", NULL };
        csilk_json_t* r = csilk_db_query_param_json(pool,
            "INSERT OR IGNORE INTO categories (user_id, name, type) VALUES (?, ?, ?)", params);
        if (r) csilk_json_free(r);
    }

    // Transaction categories
    const char* tx_cats[] = { "股票/基金", "加密货币", "债券/理财", "定期存款", NULL };
    for (int i = 0; tx_cats[i]; i++) {
        const char* params[] = { uid_str, tx_cats[i], "transaction", NULL };
        csilk_json_t* r = csilk_db_query_param_json(pool,
            "INSERT OR IGNORE INTO categories (user_id, name, type) VALUES (?, ?, ?)", params);
        if (r) csilk_json_free(r);
    }
}

/** @brief POST /api/system/setup — 首次部署系统初始化 */
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
    const char* password = csilk_json_get_string(body, "password");
    if (!username || !password || strlen(username) < 2 || strlen(password) < 6) {
        csilk_json_free(body);
        respond_bad_request(c, "用户名需≥2字符，密码需≥6字符");
        return;
    }

    // Start transaction
    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    char hashed[65];
    hash_password(password, hashed, sizeof(hashed));

    const char* insert_sql = "INSERT INTO users (username, password) VALUES (?, ?)";
    const char* insert_params[] = { username, hashed, NULL };
    csilk_json_t* ins_res = csilk_db_query_param_json(pool, insert_sql, insert_params);
    if (ins_res) csilk_json_free(ins_res);

    const char* get_params[] = { username, NULL };
    csilk_json_t* user_res = csilk_db_query_param_json(pool,
        "SELECT id, username FROM users WHERE username = ?", get_params);
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
    seed_default_categories(pool, user_id);

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);

    char* token = jwt_generate_token(c, user_id);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);

    free(token);
    respond_ok(c, resp);
}

/** @brief POST /api/auth/register — 注册（禁止在初始化后公开注册）*/
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

    int64_t user_id = db_get_int(csilk_json_array_get(user, 0), "id");
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

    int64_t user_id = db_get_int(csilk_json_array_get(result, 0), "id");
    char* token = jwt_generate_token(c, user_id);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);
    free(token);

    respond_ok(c, resp);
    csilk_json_free(result);
}

/** @brief PUT /api/auth/password — 登录后修改密码 */
void auth_change_password(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    const char* old_password = csilk_json_get_string(body, "old_password");
    const char* new_password = csilk_json_get_string(body, "new_password");
    if (!old_password || !new_password || strlen(new_password) < 6) {
        csilk_json_free(body);
        respond_bad_request(c, "原密码和新密码不能为空，新密码需≥6字符");
        return;
    }

    if (strcmp(old_password, new_password) == 0) {
        csilk_json_free(body);
        respond_bad_request(c, "新密码不能与原密码相同");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    char old_hashed[65];
    hash_password(old_password, old_hashed, sizeof(old_hashed));

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* check_params[] = { uid_str, old_hashed, NULL };
    csilk_json_t* check = csilk_db_query_param_json(pool,
        "SELECT id FROM users WHERE id = ? AND password = ?", check_params);
    if (!check || csilk_json_array_size(check) == 0) {
        if (check) csilk_json_free(check);
        csilk_json_free(body);
        respond_bad_request(c, "原密码不正确");
        return;
    }
    csilk_json_free(check);

    char new_hashed[65];
    hash_password(new_password, new_hashed, sizeof(new_hashed));

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
    csilk_json_add_number(resp, "id", db_get_num(user, "id"));
    csilk_json_add_string(resp, "username", csilk_json_get_string(user, "username"));
    csilk_json_add_string(resp, "created_at", csilk_json_get_string(user, "created_at"));
    csilk_json_free(result);

    respond_ok(c, resp);
}
