#include "application/auth/usecases.h"
#include "domain/auth/entity.h"
#include "domain/auth/rules.h"
#include "repositories/auth_repo.h"
#include "repositories/ledger_repo.h"
#include "repositories/import_rule_repo.h"
#include "controllers/category_controller.h"
#include "config/key_manager.h"
#include "common/config.h"
#include "common/jwt.h"
#include "common/totp.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include "csilk/core/bcrypt.h"
#include "csilk/core/codec.h"
#include "csilk/core/crypto_dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MINEFOLIO_BCRYPT_COST CSILK_BCRYPT_DEFAULT_COST

static void
store_bcrypt_hash(const char* password, char* out)
{
    csilk_bcrypt_hash(password, strlen(password), MINEFOLIO_BCRYPT_COST, out);
}

static int
decrypt_password_if_needed(csilk_ctx_t* c,
                           const char*  enc,
                           const char*  plain,
                           char*        out_buf,
                           size_t       out_cap,
                           const char** out_pwd,
                           size_t*      out_len)
{
    (void)c;
    *out_pwd = NULL;
    *out_len = 0;

    const char* enc_to_try = enc;
    if (!enc_to_try && plain && strlen(plain) > 100) {
        enc_to_try = plain;
    }

    if (enc_to_try && enc_to_try[0]) {
        size_t cap = out_cap;
        if (auth_key_decrypt(enc_to_try, out_buf, &cap) == 0 && cap > 0) {
            *out_pwd = out_buf;
            *out_len = cap;
            return 0;
        }
        return -1;
    }

    if (plain && plain[0]) {
        *out_pwd = plain;
        *out_len = strlen(plain);
        return 0;
    }

    return 0;
}

int
auth_usecase_register(void*                      pool,
                      const register_user_cmd_t* cmd,
                      csilk_ctx_t*               c,
                      csilk_json_t**             out_data,
                      auth_usecase_result_t*     out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || !cmd->username) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少用户名或密码");
        return -1;
    }

    char        password_buf[512] = {0};
    const char* password = NULL;
    size_t      password_len = 0;
    if (decrypt_password_if_needed(c,
                                   cmd->password_enc,
                                   cmd->plain_password,
                                   password_buf,
                                   sizeof(password_buf),
                                   &password,
                                   &password_len) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "密码解密失败");
        return -1;
    }

    char err[256];
    if (mf_auth_rule_validate_username(cmd->username, err, sizeof(err)) != 0 ||
        mf_auth_rule_validate_password(password, err, sizeof(err)) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "用户名需≥2字符，密码需≥6字符");
        return -1;
    }

    /* 检查用户名是否已存在 */
    csilk_json_t* check = user_find_by_username((csilk_db_pool_t*)pool, cmd->username);
    if (check && csilk_json_array_size(check) > 0) {
        csilk_json_free(check);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "用户名已存在");
        return -1;
    }
    if (check) {
        csilk_json_free(check);
    }

    char hashed[CSILK_BCRYPT_HASH_LEN];
    store_bcrypt_hash(password, hashed);

    int64_t user_id = user_insert((csilk_db_pool_t*)pool, cmd->username, hashed);
    if (user_id <= 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "注册失败");
        return -1;
    }

    /* 初始化默认分类、默认账本与导入规则 */
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    categories_seed_defaults(db_pool, user_id);
    ledger_get_default(db_pool, user_id);
    import_rule_seed_defaults(db_pool, user_id);

    char*         token = jwt_generate_token(c, user_id, 0);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);
    if (token) {
        free(token);
    }

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_login(void*                   pool,
                   const login_user_cmd_t* cmd,
                   csilk_ctx_t*            c,
                   csilk_json_t**          out_data,
                   auth_usecase_result_t*  out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || !cmd->username) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少用户名或密码");
        return -1;
    }

    char        password_buf[512] = {0};
    const char* password = NULL;
    size_t      password_len = 0;
    if (decrypt_password_if_needed(c,
                                   cmd->password_enc,
                                   cmd->plain_password,
                                   password_buf,
                                   sizeof(password_buf),
                                   &password,
                                   &password_len) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "密码解密失败");
        return -1;
    }

    if (!password) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少用户名或密码");
        return -1;
    }

    csilk_json_t* result = user_find_by_username((csilk_db_pool_t*)pool, cmd->username);
    if (!result || csilk_json_array_size(result) == 0) {
        if (result) {
            csilk_json_free(result);
        }
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "用户名或密码错误");
        return -1;
    }

    csilk_json_t* row = csilk_json_array_get(result, 0);
    const char*   stored_hash = csilk_json_get_string(row, "password");
    if (!stored_hash || csilk_bcrypt_verify(password, password_len, stored_hash) != 0) {
        csilk_json_free(result);
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "用户名或密码错误");
        return -1;
    }

    int64_t     user_id = db_get_int(row, "id");
    int         token_version = (int)db_get_int(row, "token_version");
    int         totp_enabled = db_get_bool(row, "totp_enabled");
    const char* totp_secret = csilk_json_get_string(row, "totp_secret");
    const char* totp_backup_codes = csilk_json_get_string(row, "totp_backup_codes");

    int is_2fa_active = (totp_secret && totp_secret[0] != '\0') &&
                        (totp_enabled || (totp_backup_codes && totp_backup_codes[0] != '\0' &&
                                          strcmp(totp_backup_codes, "[]") != 0));

    if (is_2fa_active) {
        char*         temp_token = jwt_generate_token(c, user_id, token_version);
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_bool(resp, "require_2fa", true);
        csilk_json_add_string(resp, "temp_token", temp_token ? temp_token : "");
        if (temp_token) {
            free(temp_token);
        }
        csilk_json_free(result);

        *out_data = resp;
        out_res->code = 0;
        snprintf(out_res->message, sizeof(out_res->message), "ok");
        return 0;
    }

    char*         token = jwt_generate_token(c, user_id, token_version);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);
    if (token) {
        free(token);
    }
    csilk_json_free(result);

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_me(void*                  pool,
                int64_t                user_id,
                csilk_json_t**         out_data,
                auth_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || !out_data) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    csilk_json_t* result = user_get_by_id((csilk_db_pool_t*)pool, user_id);
    if (!result || csilk_json_array_size(result) == 0) {
        if (result) {
            csilk_json_free(result);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "用户不存在");
        return -1;
    }

    csilk_json_t* user = csilk_json_array_get(result, 0);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", db_get_num(user, "id"));
    csilk_json_add_string(resp, "username", csilk_json_get_string(user, "username"));
    csilk_json_add_string(resp, "created_at", csilk_json_get_string(user, "created_at"));
    csilk_json_free(result);

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_change_password(void*                        pool,
                             const change_password_cmd_t* cmd,
                             csilk_ctx_t*                 c,
                             auth_usecase_result_t*       out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || cmd->user_id <= 0 || !cmd->old_password_enc || !cmd->new_password_enc) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码和新密码不能为空");
        return -1;
    }

    char   old_pt[512], new_pt[512];
    size_t old_len = sizeof(old_pt), new_len = sizeof(new_pt);

    if (auth_key_decrypt(cmd->old_password_enc, old_pt, &old_len) != 0 || old_len == 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码解密失败");
        return -1;
    }

    if (auth_key_decrypt(cmd->new_password_enc, new_pt, &new_len) != 0 || new_len == 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "新密码解密失败");
        return -1;
    }

    char err[256];
    if (mf_auth_rule_validate_password((const char*)new_pt, err, sizeof(err)) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "新密码需≥6字符");
        return -1;
    }

    if (strcmp((const char*)old_pt, (const char*)new_pt) == 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "新密码不能与原密码相同");
        return -1;
    }

    csilk_json_t* check = user_get_by_id((csilk_db_pool_t*)pool, cmd->user_id);
    if (!check || csilk_json_array_size(check) == 0) {
        if (check) {
            csilk_json_free(check);
        }
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码不正确");
        return -1;
    }

    const char*   username = csilk_json_get_string(csilk_json_array_get(check, 0), "username");
    csilk_json_t* full_user = user_find_by_username((csilk_db_pool_t*)pool, username);
    csilk_json_free(check);

    if (!full_user || csilk_json_array_size(full_user) == 0) {
        if (full_user) {
            csilk_json_free(full_user);
        }
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码不正确");
        return -1;
    }

    const char* stored_hash = csilk_json_get_string(csilk_json_array_get(full_user, 0), "password");
    if (!stored_hash || csilk_bcrypt_verify((const char*)old_pt, old_len, stored_hash) != 0) {
        csilk_json_free(full_user);

        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码不正确");
        return -1;
    }
    csilk_json_free(full_user);

    char new_hashed[CSILK_BCRYPT_HASH_LEN];
    store_bcrypt_hash((const char*)new_pt, new_hashed);

    user_update_password((csilk_db_pool_t*)pool, cmd->user_id, new_hashed);
    user_update_token_version((csilk_db_pool_t*)pool, cmd->user_id);

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "密码修改成功");
    return 0;
}

int
auth_usecase_2fa_status(void*                  pool,
                        int64_t                user_id,
                        csilk_json_t**         out_data,
                        auth_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || !out_data) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    csilk_json_t* user_rows = user_get_by_id((csilk_db_pool_t*)pool, user_id);
    if (!user_rows || csilk_json_array_size(user_rows) == 0) {
        if (user_rows) {
            csilk_json_free(user_rows);
        }
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    csilk_json_t* user = csilk_json_array_get(user_rows, 0);
    const char*   totp_secret = csilk_json_get_string(user, "totp_secret");
    const char*   totp_backup_codes = csilk_json_get_string(user, "totp_backup_codes");
    int           totp_enabled = db_get_bool(user, "totp_enabled");

    int is_active = (totp_secret && totp_secret[0] != '\0') &&
                    (totp_enabled || (totp_backup_codes && totp_backup_codes[0] != '\0' &&
                                      strcmp(totp_backup_codes, "[]") != 0));

    int backup_count = 0;
    if (totp_backup_codes && totp_backup_codes[0] != '\0') {
        csilk_json_t* codes_json = csilk_json_parse(totp_backup_codes);
        if (codes_json && csilk_json_is_array(codes_json)) {
            backup_count = (int)csilk_json_array_size(codes_json);
        }
        if (codes_json) {
            csilk_json_free(codes_json);
        }
    }
    csilk_json_free(user_rows);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_bool(resp, "enabled", is_active ? true : false);
    csilk_json_add_bool(resp, "has_backup_codes", backup_count > 0);
    csilk_json_add_int(resp, "backup_codes_count", backup_count);

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_2fa_setup(void*                  pool,
                       int64_t                user_id,
                       csilk_json_t**         out_data,
                       auth_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0 || !out_data) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    char secret[64];
    if (totp_generate_secret(secret, sizeof(secret)) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "生成 2FA 密钥失败");
        return -1;
    }

    user_set_totp_secret((csilk_db_pool_t*)pool, user_id, secret);

    csilk_json_t* user_rows = user_get_by_id((csilk_db_pool_t*)pool, user_id);
    const char*   uname = "user";
    if (user_rows && csilk_json_array_size(user_rows) > 0) {
        const char* u = csilk_json_get_string(csilk_json_array_get(user_rows, 0), "username");
        if (u && u[0]) {
            uname = u;
        }
    }

    char otpauth[512];
    snprintf(otpauth,
             sizeof(otpauth),
             "otpauth://totp/Minefolio:%s?secret=%s&issuer=Minefolio&digits=6&period=30",
             uname,
             secret);
    if (user_rows) {
        csilk_json_free(user_rows);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "secret", secret);
    csilk_json_add_string(resp, "otpauth_url", otpauth);

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_2fa_enable(void*                   pool,
                        const enable_2fa_cmd_t* cmd,
                        csilk_json_t**          out_data,
                        auth_usecase_result_t*  out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || cmd->user_id <= 0 || !cmd->code || strlen(cmd->code) != 6) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "请输入 6 位动态验证码");
        return -1;
    }

    csilk_json_t* user_rows = user_get_by_id((csilk_db_pool_t*)pool, cmd->user_id);
    if (!user_rows || csilk_json_array_size(user_rows) == 0) {
        if (user_rows) {
            csilk_json_free(user_rows);
        }
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    csilk_json_t* user = csilk_json_array_get(user_rows, 0);
    const char*   secret = csilk_json_get_string(user, "totp_secret");
    if (!secret || !secret[0]) {
        csilk_json_free(user_rows);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "请先生成两步验证密钥");
        return -1;
    }

    if (!totp_verify_code(secret, cmd->code)) {
        csilk_json_free(user_rows);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "动态验证码错误");
        return -1;
    }

    char backup_codes[8][16];
    totp_generate_backup_codes(backup_codes);

    csilk_json_t* codes_arr = csilk_json_array();
    for (int i = 0; i < 8; i++) {
        csilk_json_array_append(codes_arr, csilk_json_string_new(backup_codes[i]));
    }
    size_t slen = 0;
    char*  codes_json = csilk_json_serialize(codes_arr, &slen);

    user_enable_totp((csilk_db_pool_t*)pool, cmd->user_id, codes_json ? codes_json : "[]");
    if (codes_json) {
        free(codes_json);
    }
    csilk_json_free(user_rows);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_array(resp, "backup_codes", codes_arr);

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_2fa_disable(void* pool, int64_t user_id, auth_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || user_id <= 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    user_disable_totp((csilk_db_pool_t*)pool, user_id);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_2fa_verify_login(void*                         pool,
                              const verify_2fa_login_cmd_t* cmd,
                              csilk_json_t**                out_data,
                              auth_usecase_result_t*        out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || !cmd->temp_token || !cmd->code || !cmd->code[0]) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少 temp_token 或验证码");
        return -1;
    }

    const char* secret_jwt = config_secret_get("JWT_SECRET", NULL, 0);
    if (!secret_jwt) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "JWT secret 未配置");
        return -1;
    }

    csilk_json_t* payload = csilk_jwt_verify(NULL, cmd->temp_token, secret_jwt);
    if (!payload) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    int64_t user_id = db_get_int(payload, "sub");
    csilk_json_free(payload);
    if (user_id <= 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    csilk_json_t* user_rows = user_get_by_id((csilk_db_pool_t*)pool, user_id);
    if (!user_rows || csilk_json_array_size(user_rows) == 0) {
        if (user_rows) {
            csilk_json_free(user_rows);
        }
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    csilk_json_t* user_row = csilk_json_array_get(user_rows, 0);
    const char*   totp_secret = csilk_json_get_string(user_row, "totp_secret");
    const char*   backup_codes = csilk_json_get_string(user_row, "totp_backup_codes");
    int           token_version = (int)db_get_int(user_row, "token_version");

    bool ok = false;
    if (totp_verify_code(totp_secret, cmd->code)) {
        ok = true;
    } else if (backup_codes && backup_codes[0]) {
        char updated_codes[1024];
        if (totp_verify_and_consume_backup_code(
                backup_codes, cmd->code, updated_codes, sizeof(updated_codes))) {
            user_update_backup_codes((csilk_db_pool_t*)pool, user_id, updated_codes);
            ok = true;
        }
    }
    csilk_json_free(user_rows);

    if (!ok) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "动态验证码错误或备用码已失效");
        return -1;
    }

    char*         token = jwt_generate_token(NULL, user_id, token_version);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);
    if (token) {
        free(token);
    }

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_oauth_providers(csilk_json_t** out_data, auth_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!out_data) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    csilk_json_t* providers = csilk_json_array();
    const char*   gh_client_id = config_secret_get("OAUTH_GITHUB_CLIENT_ID", NULL, 0);
    if (gh_client_id && gh_client_id[0]) {
        csilk_json_t* gh = csilk_json_object();
        csilk_json_add_string(gh, "id", "github");
        csilk_json_add_string(gh, "name", "GitHub");
        csilk_json_add_string(gh, "icon", "ph:github-logo");
        char auth_url[512];
        snprintf(auth_url,
                 sizeof(auth_url),
                 "https://github.com/login/oauth/authorize?client_id=%s&scope=read:user,user:email",
                 gh_client_id);
        csilk_json_add_string(gh, "auth_url", auth_url);
        csilk_json_array_append(providers, gh);
    }

    const char* oidc_client_id = config_secret_get("OAUTH_OIDC_CLIENT_ID", NULL, 0);
    const char* oidc_auth_url = config_env_get("OAUTH_OIDC_AUTH_URL", NULL, 0, NULL);
    if (oidc_client_id && oidc_client_id[0] && oidc_auth_url && oidc_auth_url[0]) {
        csilk_json_t* oidc = csilk_json_object();
        csilk_json_add_string(oidc, "id", "oidc");
        csilk_json_add_string(oidc, "name", "Single Sign-On (OIDC)");
        csilk_json_add_string(oidc, "icon", "ph:key");
        char full_auth_url[512];
        snprintf(full_auth_url,
                 sizeof(full_auth_url),
                 "%s?client_id=%s&response_type=code&scope=openid%%20profile%%20email",
                 oidc_auth_url,
                 oidc_client_id);
        csilk_json_add_string(oidc, "auth_url", full_auth_url);
        csilk_json_array_append(providers, oidc);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_array(resp, "providers", providers);

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_oauth_callback(void*                       pool,
                            const oauth_callback_cmd_t* cmd,
                            csilk_json_t**              out_data,
                            auth_usecase_result_t*      out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || !cmd->provider || !cmd->provider[0]) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "OAuth 提供商 (provider) 不能为空");
        return -1;
    }

    char provider_buf[64] = {0};
    strncpy(provider_buf, cmd->provider, sizeof(provider_buf) - 1);

    char oauth_id_buf[128] = {0};
    char username_buf[128] = {0};

    if (cmd->oauth_id && cmd->oauth_id[0]) {
        strncpy(oauth_id_buf, cmd->oauth_id, sizeof(oauth_id_buf) - 1);
    } else if (cmd->code && cmd->code[0]) {
        snprintf(oauth_id_buf, sizeof(oauth_id_buf), "sub_%s", cmd->code);
    } else {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "授权码 (code) 或 oauth_id 不能为空");
        return -1;
    }

    if (cmd->username && cmd->username[0]) {
        strncpy(username_buf, cmd->username, sizeof(username_buf) - 1);
    } else {
        snprintf(username_buf, sizeof(username_buf), "%s_%s", provider_buf, oauth_id_buf);
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    user_row = user_find_by_oauth(db_pool, provider_buf, oauth_id_buf);

    int64_t user_id = -1;
    int     token_version = 0;
    char    final_username[128] = {0};

    if (user_row && csilk_json_array_size(user_row) > 0) {
        csilk_json_t* u = csilk_json_array_get(user_row, 0);
        user_id = db_get_int(u, "id");
        token_version = (int)db_get_int(u, "token_version");
        const char* un = csilk_json_get_string(u, "username");
        if (un) {
            strncpy(final_username, un, sizeof(final_username) - 1);
        }
        csilk_json_free(user_row);
    } else {
        if (user_row) {
            csilk_json_free(user_row);
        }

        csilk_json_t* check_un = user_find_by_username(db_pool, username_buf);
        if (check_un && csilk_json_array_size(check_un) > 0) {
            snprintf(final_username,
                     sizeof(final_username),
                     "%s_%d",
                     username_buf,
                     (int)(time(NULL) % 10000));
        } else {
            strncpy(final_username, username_buf, sizeof(final_username) - 1);
        }
        if (check_un) {
            csilk_json_free(check_un);
        }

        user_id = user_create_oauth(db_pool, final_username, provider_buf, oauth_id_buf);
        if (user_id <= 0) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "创建 OAuth 关联用户失败");
            return -1;
        }

        categories_seed_defaults(db_pool, user_id);
        import_rule_seed_defaults(db_pool, user_id);
        ledger_get_default(db_pool, user_id);
    }

    char*         token = jwt_generate_token(NULL, user_id, token_version);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);

    csilk_json_t* u_obj = csilk_json_object();
    csilk_json_add_number(u_obj, "id", (double)user_id);
    csilk_json_add_string(u_obj, "username", final_username);
    csilk_json_add_string(u_obj, "oauth_provider", provider_buf);
    csilk_json_add_object(resp, "user", u_obj);

    if (token) {
        free(token);
    }
    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}
