#include "application/auth/usecases.h"
#include "domain/auth/entity.h"
#include "domain/auth/rules.h"
#include "domain/auth/repository.h"
#include "infrastructure/repositories/auth_repo_impl.h"
#include "common/ledger_utils.h"
#include "infrastructure/repositories/import_rule_repo_impl.h"
#include "application/category/usecases.h"
#include "application/account/usecases.h"
#include "application/skill/usecases.h"
#include "config/key_manager.h"
#include "common/config.h"
#include "common/jwt.h"
#include "csilk/core/totp.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include "csilk/crypto/bcrypt.h"
#include "csilk/core/primitives/codec.h"
#include "csilk/crypto/crypto_dispatch.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MINEFOLIO_BCRYPT_COST CSILK_BCRYPT_DEFAULT_COST

/* TOTP 验证包装：在 csilk_totp_verify_code (±1 步容差) 基础上扩展至 ±3 步 (±90s)，
   吸收容器/宿主机时钟漂移 (NTP 未同步、时区配置错误、手机本地时钟偏移) 与空白字符，
   彻底解决移动端空格/时钟漂移导致的误验。验证失败时回填服务器当前 Unix 时间戳，供前端
   提示用户校准本地时钟。不动 csilk 依赖 (totp.c 在 deps/)，仅在应用层包一层。 */
static bool
mf_totp_verify_lenient(const char* base32_secret, const char* code, uint64_t* out_server_ts)
{
    uint64_t now = (uint64_t)time(NULL);
    if (out_server_ts) {
        *out_server_ts = now;
    }
    if (!base32_secret || !base32_secret[0] || !code) {
        return false;
    }
    /* 过滤空白字符 */
    char   clean[16] = {0};
    size_t clean_len = 0;
    for (size_t i = 0; code[i] && clean_len < sizeof(clean) - 1; i++) {
        unsigned char ch = (unsigned char)code[i];
        if (ch > ' ') {
            clean[clean_len++] = (char)ch;
        }
    }
    clean[clean_len] = '\0';
    if (clean_len != 6) {
        return false;
    }
    for (int step = -3; step <= 3; step++) {
        uint64_t t = (int64_t)now + (int64_t)step * 30;
        char     expected[8];
        if (csilk_totp_generate_code(base32_secret, t, expected, sizeof(expected)) == 0 &&
            strcmp(expected, clean) == 0) {
            return true;
        }
    }
    return false;
}

static void
store_bcrypt_hash(const char* password, char* out)
{
    csilk_bcrypt_hash(password, strlen(password), MINEFOLIO_BCRYPT_COST, out);
}

/* OAuth mock 仅在启动期从 MINEFOLIO_TEST_OAUTH_MOCK 环境变量固化一次。
   禁止在请求路径反复 getenv（原实现在每次 callback 都 getenv，生产误设该变量
   即可伪造任意 OAuth 用户）。环境变量在进程启动时由测试桩/CI 设定，运行期
   不再重新读取；测试可在进程内通过 reset 重新固化以模拟不同的启动环境。 */
static int s_oauth_mock_enabled = -1; /* -1 未解析 */
static int
oauth_mock_enabled(void)
{
    if (s_oauth_mock_enabled < 0) {
        const char* e = getenv("MINEFOLIO_TEST_OAUTH_MOCK");
        s_oauth_mock_enabled = (e && strcmp(e, "1") == 0) ? 1 : 0;
    }
    return s_oauth_mock_enabled;
}

void
auth_oauth_mock_reset(void)
{
    s_oauth_mock_enabled = -1;
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
    mf_user_t check_user = {0};
    if (mf_auth_repo_find_by_username(pool, cmd->username, &check_user) == 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "用户名已存在");
        return -1;
    }

    /* 注册开放门禁：首个用户（系统初始化引导）必须放行；已有用户时受
       ENABLE_REGISTRATION 配置控制（默认开放，可关闭以阻止后续注册）。
       该规则此前仅有单测、生产注册路径无调用点，此处接入。 */
    {
        int         user_count = mf_auth_repo_count(pool);
        char        reg_cfg[32] = {0};
        const char* v = config_env_get("ENABLE_REGISTRATION", reg_cfg, sizeof(reg_cfg), "1");
        bool        allow_registration = !v || v[0] == '1' || strcasecmp(v, "true") == 0;
        if (!mf_auth_rule_can_register(user_count, allow_registration)) {
            out_res->code = 1004;
            snprintf(out_res->message, sizeof(out_res->message), "当前系统已关闭用户注册");
            return -1;
        }
    }

    char hashed[CSILK_BCRYPT_HASH_LEN];
    store_bcrypt_hash(password, hashed);

    int64_t user_id = 0;
    if (mf_auth_repo_create(pool, cmd->username, hashed, &user_id) != 0 || user_id <= 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "注册失败");
        return -1;
    }

    /* 初始化默认账本，再按账本播种默认分类与导入规则 */
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    int64_t          ledger_id = ledger_get_default(db_pool, user_id);
    category_usecase_seed_defaults(db_pool, user_id, ledger_id);
    account_usecase_seed_default(db_pool, user_id, ledger_id);
    mf_import_rule_seed_defaults(db_pool, user_id);
    skill_usecase_seed_defaults(db_pool, user_id);

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

    mf_user_t user = {0};
    if (mf_auth_repo_find_by_username(pool, cmd->username, &user) != 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "用户名或密码错误");
        return -1;
    }

    if (csilk_bcrypt_verify(password, password_len, user.password_hash) != 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "用户名或密码错误");
        return -1;
    }

    int is_2fa_active = (user.totp_secret[0] != '\0') &&
                        (user.totp_enabled || (user.totp_backup_codes[0] != '\0' &&
                                               strcmp(user.totp_backup_codes, "[]") != 0));

    if (is_2fa_active) {
        char*         temp_token = jwt_generate_temp_token(c, user.id, user.token_version, 300);
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_bool(resp, "require_2fa", true);
        csilk_json_add_string(resp, "temp_token", temp_token ? temp_token : "");
        if (temp_token) {
            free(temp_token);
        }

        *out_data = resp;
        out_res->code = 0;
        snprintf(out_res->message, sizeof(out_res->message), "ok");
        return 0;
    }

    char*         token = jwt_generate_token(c, user.id, user.token_version);
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

    mf_user_t user = {0};
    if (mf_auth_repo_get_by_id(pool, user_id, &user) != 0) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "用户不存在");
        return -1;
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", (double)user.id);
    csilk_json_add_string(resp, "username", user.username);
    csilk_json_add_string(resp, "created_at", user.created_at);

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

    mf_user_t check = {0};
    if (mf_auth_repo_get_by_id(pool, cmd->user_id, &check) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码不正确");
        return -1;
    }

    mf_user_t full_user = {0};
    if (mf_auth_repo_find_by_username(pool, check.username, &full_user) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码不正确");
        return -1;
    }

    if (csilk_bcrypt_verify((const char*)old_pt, old_len, full_user.password_hash) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码不正确");
        return -1;
    }

    char new_hashed[CSILK_BCRYPT_HASH_LEN];
    store_bcrypt_hash((const char*)new_pt, new_hashed);

    mf_auth_repo_update_password(pool, cmd->user_id, new_hashed);
    mf_auth_repo_update_token_version(pool, cmd->user_id);

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

    mf_user_t user = {0};
    if (mf_auth_repo_get_by_id(pool, user_id, &user) != 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    int is_active = (user.totp_secret[0] != '\0') &&
                    (user.totp_enabled || (user.totp_backup_codes[0] != '\0' &&
                                           strcmp(user.totp_backup_codes, "[]") != 0));

    int backup_count = 0;
    if (user.totp_backup_codes[0] != '\0') {
        csilk_json_t* codes_json = csilk_json_parse(user.totp_backup_codes);
        if (codes_json && csilk_json_is_array(codes_json)) {
            backup_count = (int)csilk_json_array_size(codes_json);
        }
        if (codes_json) {
            csilk_json_free(codes_json);
        }
    }

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
    if (csilk_totp_generate_secret(secret, sizeof(secret)) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "生成 2FA 密钥失败");
        return -1;
    }

    mf_auth_repo_set_totp_secret(pool, user_id, secret);

    mf_user_t   user = {0};
    const char* uname = "user";
    if (mf_auth_repo_get_by_id(pool, user_id, &user) == 0 && user.username[0]) {
        uname = user.username;
    }

    char otpauth[512];
    csilk_totp_build_uri("Minefolio", uname, secret, otpauth, sizeof(otpauth));

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
    char   clean[16] = {0};
    size_t clean_len = 0;
    if (cmd && cmd->code) {
        for (size_t i = 0; cmd->code[i] && clean_len < sizeof(clean) - 1; i++) {
            unsigned char ch = (unsigned char)cmd->code[i];
            if (ch > ' ') {
                clean[clean_len++] = (char)ch;
            }
        }
    }
    clean[clean_len] = '\0';

    if (!pool || !cmd || cmd->user_id <= 0 || clean_len != 6) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "请输入 6 位动态验证码");
        return -1;
    }

    mf_user_t user = {0};
    if (mf_auth_repo_get_by_id(pool, cmd->user_id, &user) != 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    if (!user.totp_secret[0]) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "请先生成两步验证密钥");
        return -1;
    }

    uint64_t _ts_unused;
    if (!mf_totp_verify_lenient(user.totp_secret, clean, &_ts_unused)) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "动态验证码错误 (服务端时间 %llu)，请校准时钟后重试",
                 (unsigned long long)_ts_unused);
        return -1;
    }

    char backup_codes[8][16];
    csilk_totp_generate_backup_codes(backup_codes);

    csilk_json_t* codes_arr = csilk_json_array();
    for (int i = 0; i < 8; i++) {
        csilk_json_array_append(codes_arr, csilk_json_string_new(backup_codes[i]));
    }
    size_t slen = 0;
    char*  codes_json = csilk_json_serialize(codes_arr, &slen);

    mf_auth_repo_enable_totp(pool, cmd->user_id, codes_json ? codes_json : "[]");
    if (codes_json) {
        free(codes_json);
    }

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_array(resp, "backup_codes", codes_arr);

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_2fa_disable(void*                    pool,
                         const disable_2fa_cmd_t* cmd,
                         csilk_ctx_t*             c,
                         auth_usecase_result_t*   out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || cmd->user_id <= 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    mf_user_t user = {0};
    if (mf_auth_repo_get_by_id(pool, cmd->user_id, &user) != 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    bool authenticated = false;

    /* 1. 验证登录密码（支持明文或加密密文） */
    char        pw_buf[256] = {0};
    const char* pwd = NULL;
    size_t      pwd_len = 0;
    decrypt_password_if_needed(
        c, cmd->password_enc, cmd->password, pw_buf, sizeof(pw_buf) - 1, &pwd, &pwd_len);

    if (pwd && pwd_len > 0 && user.password_hash[0]) {
        if (csilk_bcrypt_verify(pwd, pwd_len, user.password_hash) == 0) {
            authenticated = true;
        }
    }

    /* 2. 或验证当前有效的 6 位 TOTP 动态码 (lenient ±3 步容差，过滤空白，吸收时钟漂移) */
    if (!authenticated && cmd->code && cmd->code[0] && user.totp_secret[0]) {
        uint64_t _ts_unused;
        if (mf_totp_verify_lenient(user.totp_secret, cmd->code, &_ts_unused)) {
            authenticated = true;
        }
    }

    if (!authenticated) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "解绑 2FA 需验证当前密码或有效 6 位动态验证码");
        return -1;
    }

    mf_auth_repo_disable_totp(pool, cmd->user_id);
    mf_auth_repo_update_token_version(pool, cmd->user_id);
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

    const char* token_type = csilk_json_get_string(payload, "type");
    if (!token_type || strcmp(token_type, "2fa_pending") != 0) {
        csilk_json_free(payload);
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "无效的 2FA 临时凭证");
        return -1;
    }

    int64_t user_id = db_get_int(payload, "sub");
    int     token_v = (int)db_get_int(payload, "v");
    csilk_json_free(payload);
    if (user_id <= 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    mf_user_t user = {0};
    if (mf_auth_repo_get_by_id(pool, user_id, &user) != 0 || user.token_version != token_v) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权或临时凭证已失效");
        return -1;
    }

    /* 规范化输入：过滤所有空白字符并生成大小写变体（兼顾 TOTP 动态码中间空格与备份码大小写容错） */
    char   clean_code[32] = {0};
    char   clean_lower[32] = {0};
    char   clean_upper[32] = {0};
    size_t clean_len = 0;
    if (cmd->code) {
        for (size_t i = 0; cmd->code[i] && clean_len < sizeof(clean_code) - 1; i++) {
            unsigned char ch = (unsigned char)cmd->code[i];
            if (ch > ' ') {
                clean_code[clean_len] = (char)ch;
                clean_lower[clean_len] = (char)((ch >= 'A' && ch <= 'Z') ? (ch + ('a' - 'A')) : ch);
                clean_upper[clean_len] = (char)((ch >= 'a' && ch <= 'z') ? (ch - ('a' - 'A')) : ch);
                clean_len++;
            }
        }
    }
    clean_code[clean_len] = '\0';
    clean_lower[clean_len] = '\0';
    clean_upper[clean_len] = '\0';

    bool     ok = false;
    uint64_t server_ts = (uint64_t)time(NULL);
    if (clean_len == 6 && mf_totp_verify_lenient(user.totp_secret, clean_code, &server_ts)) {
        ok = true;
    } else if (user.totp_backup_codes[0] != '\0') {
        char updated_codes[1024];
        /* 依次按原输入、全小写、全大写尝试匹配，完全消除备份码大小写差异 */
        if (csilk_totp_verify_and_consume_backup_code(
                user.totp_backup_codes, clean_code, updated_codes, sizeof(updated_codes)) ||
            (strcmp(clean_code, clean_lower) != 0 &&
             csilk_totp_verify_and_consume_backup_code(
                 user.totp_backup_codes, clean_lower, updated_codes, sizeof(updated_codes))) ||
            (strcmp(clean_code, clean_upper) != 0 &&
             csilk_totp_verify_and_consume_backup_code(
                 user.totp_backup_codes, clean_upper, updated_codes, sizeof(updated_codes)))) {
            mf_auth_repo_update_backup_codes(pool, user_id, updated_codes);
            ok = true;
        }
    }

    if (!ok) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "动态验证码错误或备用码已失效 (服务端时间 "
                 "%llu)。若反复失败，请先校准手机/电脑时钟至 UTC 再试",
                 (unsigned long long)server_ts);
        /* 把服务器时间戳挂到 data，供前端展示诊断 */
        csilk_json_t* diag = csilk_json_object();
        csilk_json_add_number(diag, "server_unix_ts", (double)server_ts);
        csilk_json_add_string(
            diag, "hint", "服务端时钟与本地时钟偏差可能超过 60s，请校准 NTP 后重试");
        *out_data = diag;
        return -1;
    }

    char*         token = jwt_generate_token(NULL, user_id, user.token_version);
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

typedef struct {
    char*  data;
    size_t size;
    size_t cap;
} oauth_curl_buf_t;

static size_t
oauth_curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t            total = size * nmemb;
    oauth_curl_buf_t* b = (oauth_curl_buf_t*)userdata;
    if (b->size + total + 1 > b->cap) {
        size_t new_cap = (b->cap + total + 1) * 2;
        char*  nd = realloc(b->data, new_cap);
        if (!nd) {
            return 0;
        }
        b->data = nd;
        b->cap = new_cap;
    }
    memcpy(b->data + b->size, ptr, total);
    b->size += total;
    b->data[b->size] = '\0';
    return total;
}

static int
oauth_exchange_github(
    const char* code, char* out_oauth_id, size_t id_cap, char* out_username, size_t un_cap)
{
    const char* client_id = config_secret_get("OAUTH_GITHUB_CLIENT_ID", NULL, 0);
    const char* client_secret = config_secret_get("OAUTH_GITHUB_CLIENT_SECRET", NULL, 0);
    if (!client_id || !client_id[0] || !client_secret || !client_secret[0]) {
        return -2; /* 未配置 */
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    char post_fields[512];
    snprintf(post_fields,
             sizeof(post_fields),
             "client_id=%s&client_secret=%s&code=%s",
             client_id,
             client_secret,
             code);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: Minefolio-Backend");

    oauth_curl_buf_t buf = {.data = malloc(4096), .size = 0, .cap = 4096};
    if (buf.data) {
        buf.data[0] = '\0';
    }

    curl_easy_setopt(curl, CURLOPT_URL, "https://github.com/login/oauth/access_token");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, oauth_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long     http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK || http_code != 200 || !buf.data || buf.size == 0) {
        if (buf.data) {
            free(buf.data);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }

    csilk_json_t* tok_resp = csilk_json_parse(buf.data);
    free(buf.data);
    if (!tok_resp) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }
    const char* access_tok = csilk_json_get_string(tok_resp, "access_token");
    if (!access_tok || !access_tok[0]) {
        csilk_json_free(tok_resp);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }

    char auth_hdr[512];
    snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", access_tok);
    curl_slist_free_all(headers);
    headers = NULL;
    headers = curl_slist_append(headers, auth_hdr);
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: Minefolio-Backend");
    csilk_json_free(tok_resp);

    oauth_curl_buf_t user_buf = {.data = malloc(8192), .size = 0, .cap = 8192};
    if (user_buf.data) {
        user_buf.data[0] = '\0';
    }

    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/user");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &user_buf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200 || !user_buf.data || user_buf.size == 0) {
        if (user_buf.data) {
            free(user_buf.data);
        }
        return -1;
    }

    csilk_json_t* u_resp = csilk_json_parse(user_buf.data);
    free(user_buf.data);
    if (!u_resp) {
        return -1;
    }

    int64_t     gh_id = (int64_t)csilk_json_get_number(u_resp, "id");
    const char* gh_login = csilk_json_get_string(u_resp, "login");
    if (gh_id <= 0) {
        csilk_json_free(u_resp);
        return -1;
    }
    snprintf(out_oauth_id, id_cap, "%lld", (long long)gh_id);
    snprintf(out_username, un_cap, "%s", gh_login ? gh_login : out_oauth_id);
    csilk_json_free(u_resp);
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

    if (!cmd->code || !cmd->code[0]) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "授权码 (code) 不能为空");
        return -1;
    }

    char provider_buf[64] = {0};
    strncpy(provider_buf, cmd->provider, sizeof(provider_buf) - 1);

    char oauth_id_buf[128] = {0};
    char username_buf[128] = {0};

    /* 启动期固化的 OAuth mock 开关，见 oauth_mock_enabled() */
    if (oauth_mock_enabled()) {
        if (strncmp(cmd->code, "mock_code_", 10) == 0) {
            snprintf(oauth_id_buf, sizeof(oauth_id_buf), "sub_%s", cmd->code + 10);
            snprintf(username_buf,
                     sizeof(username_buf),
                     "%s",
                     cmd->username && cmd->username[0] ? cmd->username : oauth_id_buf);
        } else {
            out_res->code = 1002;
            snprintf(
                out_res->message, sizeof(out_res->message), "OAuth 授权置换失败 (Mock 拒识 Code)");
            return -1;
        }
    } else {
        if (strcmp(provider_buf, "github") == 0) {
            int ex_rc = oauth_exchange_github(
                cmd->code, oauth_id_buf, sizeof(oauth_id_buf), username_buf, sizeof(username_buf));
            if (ex_rc == -2) {
                out_res->code = 1002;
                snprintf(
                    out_res->message, sizeof(out_res->message), "GitHub OAuth 服务端配置未就绪");
                return -1;
            } else if (ex_rc != 0) {
                out_res->code = 1002;
                snprintf(out_res->message, sizeof(out_res->message), "OAuth 授权置换失败");
                return -1;
            }
        } else {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "不支持的 OAuth 提供商");
            return -1;
        }
    }

    mf_user_t oauth_user = {0};
    int       rc = mf_auth_repo_find_by_oauth(pool, provider_buf, oauth_id_buf, &oauth_user);

    int64_t user_id = -1;
    int     token_version = 0;
    char    final_username[128] = {0};

    if (rc == 0) {
        user_id = oauth_user.id;
        token_version = (int)oauth_user.token_version;
        snprintf(final_username, sizeof(final_username), "%s", oauth_user.username);
    } else {
        mf_user_t check_un = {0};
        if (mf_auth_repo_find_by_username(pool, username_buf, &check_un) == 0) {
            snprintf(final_username,
                     sizeof(final_username),
                     "%s_%d",
                     username_buf,
                     (int)(time(NULL) % 10000));
        } else {
            snprintf(final_username, sizeof(final_username), "%s", username_buf);
        }

        if (mf_auth_repo_create_oauth(pool, final_username, provider_buf, oauth_id_buf, &user_id) !=
                0 ||
            user_id <= 0) {
            out_res->code = 1002;
            snprintf(out_res->message, sizeof(out_res->message), "创建 OAuth 关联用户失败");
            return -1;
        }

        csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
        int64_t          ledger_id = ledger_get_default(db_pool, user_id);
        category_usecase_seed_defaults(db_pool, user_id, ledger_id);
        account_usecase_seed_default(db_pool, user_id, ledger_id);
        mf_import_rule_seed_defaults(db_pool, user_id);
        skill_usecase_seed_defaults(db_pool, user_id);
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
