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
#include "common/http_safety.h"
#include "common/legacy_bcrypt.h"
#include "csilk/csilk.h"
#include "csilk/crypto/bcrypt.h"
#include "csilk/core/primitives/codec.h"
#include "csilk/crypto/crypto_dispatch.h"
#include "csilk/drivers/cipher.h"
#include <curl/curl.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define MINEFOLIO_BCRYPT_COST CSILK_BCRYPT_DEFAULT_COST

static _Atomic(mf_auth_jwt_invalidator_fn) g_jwt_invalidator = NULL;

void
auth_usecase_set_jwt_invalidator(mf_auth_jwt_invalidator_fn fn)
{
    atomic_store_explicit(&g_jwt_invalidator, fn, memory_order_release);
}

static void
invalidate_user_jwt(int64_t user_id)
{
    mf_auth_jwt_invalidator_fn fn = atomic_load_explicit(&g_jwt_invalidator, memory_order_acquire);
    if (fn) {
        fn(user_id);
    }
}

/* 规范化两步验证码（提取干净的 lowercase 字符集以及纯数字流）：
   支持处理全角数字 (０-９)、全角英文字母 (ａ-ｚ/Ａ-Ｚ)、各类破折号 (—, –, −, －)、
   零宽字符 (\u200B-\u200D)、BOM (\uFEFF)、全角空格及不换行空格。 */
static size_t
normalize_auth_code(const char* in, char* out_clean, char* out_digits, size_t cap)
{
    if (!in || !out_clean || cap == 0) {
        if (out_clean && cap > 0) {
            out_clean[0] = '\0';
        }
        if (out_digits && cap > 0) {
            out_digits[0] = '\0';
        }
        return 0;
    }
    size_t               clean_len = 0;
    size_t               digits_len = 0;
    const unsigned char* p = (const unsigned char*)in;

    while (*p && clean_len + 1 < cap) {
        /* 1. UTF-8 3-byte sequences */
        if (p[0] == 0xEF && p[1] == 0xBC && p[2] >= 0x90 && p[2] <= 0x99) {
            /* 全角数字 ０-９ -> '0'-'9' */
            char d = (char)('0' + (p[2] - 0x90));
            out_clean[clean_len++] = d;
            if (out_digits && digits_len + 1 < cap) {
                out_digits[digits_len++] = d;
            }
            p += 3;
            continue;
        }
        if (p[0] == 0xEF && p[1] == 0xBC && p[2] >= 0xA1 && p[2] <= 0xBA) {
            /* 全角大写 Ａ-Ｚ -> 'A'-'Z' */
            char ch = (char)('A' + (p[2] - 0xA1));
            out_clean[clean_len++] = ch;
            p += 3;
            continue;
        }
        if (p[0] == 0xEF && p[1] == 0xBD && p[2] >= 0x81 && p[2] <= 0x9A) {
            /* 全角小写 ａ-ｚ -> 'a'-'z' */
            char ch = (char)('a' + (p[2] - 0x81));
            out_clean[clean_len++] = ch;
            p += 3;
            continue;
        }
        if ((p[0] == 0xEF && p[1] == 0xBC && p[2] == 0x8D) ||
            (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x94 || p[2] == 0x93)) ||
            (p[0] == 0xE2 && p[1] == 0x88 && p[2] == 0x92)) {
            /* 全角/长短破折号/减号 －, —, –, − -> '-' */
            out_clean[clean_len++] = '-';
            p += 3;
            continue;
        }
        if ((p[0] == 0xE2 && p[1] == 0x80 && (p[2] >= 0x8B && p[2] <= 0x8D)) ||
            (p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) ||
            (p[0] == 0xE3 && p[1] == 0x80 && p[2] == 0x80)) {
            /* 零宽空格/BOM/全角空格 -> 忽略 */
            p += 3;
            continue;
        }
        /* 2. UTF-8 2-byte sequences */
        if (p[0] == 0xC2 && p[1] == 0xA0) {
            /* 不换行空格 \u00A0 -> 忽略 */
            p += 2;
            continue;
        }
        /* 3. ASCII characters */
        unsigned char ch = *p++;
        if (ch <= ' ' || ch == 127) {
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            out_clean[clean_len++] = (char)ch;
            if (out_digits && digits_len + 1 < cap) {
                out_digits[digits_len++] = (char)ch;
            }
        } else if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
            out_clean[clean_len++] = (char)ch;
        } else if (ch == '-') {
            out_clean[clean_len++] = '-';
        }
    }
    out_clean[clean_len] = '\0';
    if (out_digits) {
        out_digits[digits_len] = '\0';
    }
    return clean_len;
}

/* 可配置的 TOTP 时钟偏移步长上限：默认 ±2 步（±60s），允许通过 MINEFOLIO_TOTP_WINDOW_STEPS 在 [1, 10] 内调整。 */
static int
mf_totp_window_steps(void)
{
    char        buf[32] = {0};
    const char* raw = config_env_get("MINEFOLIO_TOTP_WINDOW_STEPS", buf, sizeof(buf), NULL);
    if (!raw || !raw[0]) {
        return 2;
    }
    char* end = NULL;
    long  v = strtol(raw, &end, 10);
    if (!end || *end != '\0' || v < 1) {
        return 2;
    }
    if (v > 10) {
        return 10;
    }
    return (int)v;
}

#define MF_TOTP_REPLAY_CAP 1024

typedef struct {
    bool    in_use;
    int64_t user_id;
    int64_t last_step;
    time_t  updated_at;
} mf_totp_replay_entry_t;

static mf_totp_replay_entry_t g_totp_replay[MF_TOTP_REPLAY_CAP];
static pthread_mutex_t        g_totp_replay_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 基于进程内滑动步长防重放：同一用户在相同或更早 TOTP 时间步内禁止重复使用动态码。 */
static bool
totp_replay_check_and_record(int64_t user_id, int64_t matched_step)
{
    if (user_id <= 0) {
        return false;
    }
    if (pthread_mutex_lock(&g_totp_replay_mutex) != 0) {
        return false;
    }

    int    free_idx = -1;
    int    oldest_idx = 0;
    time_t oldest_ts = g_totp_replay[0].updated_at;
    time_t now_ts = time(NULL);

    for (int i = 0; i < MF_TOTP_REPLAY_CAP; i++) {
        if (!g_totp_replay[i].in_use) {
            if (free_idx < 0) {
                free_idx = i;
            }
            continue;
        }
        if (g_totp_replay[i].user_id == user_id) {
            bool allowed = (matched_step > g_totp_replay[i].last_step);
            if (allowed) {
                g_totp_replay[i].last_step = matched_step;
                g_totp_replay[i].updated_at = now_ts;
            }
            pthread_mutex_unlock(&g_totp_replay_mutex);
            return allowed;
        }
        if (g_totp_replay[i].updated_at < oldest_ts) {
            oldest_ts = g_totp_replay[i].updated_at;
            oldest_idx = i;
        }
    }

    int target = (free_idx >= 0) ? free_idx : oldest_idx;
    g_totp_replay[target].in_use = true;
    g_totp_replay[target].user_id = user_id;
    g_totp_replay[target].last_step = matched_step;
    g_totp_replay[target].updated_at = now_ts;
    pthread_mutex_unlock(&g_totp_replay_mutex);
    return true;
}

static void
totp_replay_clear_user(int64_t user_id)
{
    if (user_id <= 0) {
        return;
    }
    if (pthread_mutex_lock(&g_totp_replay_mutex) != 0) {
        return;
    }
    for (int i = 0; i < MF_TOTP_REPLAY_CAP; i++) {
        if (g_totp_replay[i].in_use && g_totp_replay[i].user_id == user_id) {
            memset(&g_totp_replay[i], 0, sizeof(g_totp_replay[i]));
            break;
        }
    }
    pthread_mutex_unlock(&g_totp_replay_mutex);
}

/* TOTP 验证包装：按配置步长窗口（默认 ±2 步 = ±60s，可通过 MINEFOLIO_TOTP_WINDOW_STEPS 调整）
   校验动态码，使用 CRYPTO_memcmp 常时间比较避免时序侧信道。
   验证失败时回填服务器当前 Unix 时间戳，供前端提示用户校准本地时钟。 */
static bool
mf_totp_verify_lenient(const char* base32_secret,
                       const char* code,
                       uint64_t*   out_server_ts,
                       int64_t*    out_matched_step)
{
    uint64_t now = (uint64_t)time(NULL);
    if (out_server_ts) {
        *out_server_ts = now;
    }
    if (out_matched_step) {
        *out_matched_step = 0;
    }
    if (!base32_secret || !base32_secret[0] || !code) {
        return false;
    }
    char clean[32] = {0};
    char digits[32] = {0};
    normalize_auth_code(code, clean, digits, sizeof(clean));
    if (strlen(digits) != 6) {
        return false;
    }
    int     window = mf_totp_window_steps();
    int64_t base_step = (int64_t)(now / 30ULL);
    for (int step = -window; step <= window; step++) {
        int64_t cand_step = base_step + step;
        if (cand_step < 0) {
            continue;
        }
        uint64_t t = (uint64_t)cand_step * 30ULL;
        char     expected[8] = {0};
        if (csilk_totp_generate_code(base32_secret, t, expected, sizeof(expected)) == 0 &&
            strlen(expected) == 6 && CRYPTO_memcmp(expected, digits, 6) == 0) {
            if (out_matched_step) {
                *out_matched_step = cand_step;
            }
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

int
auth_usecase_verify_and_upgrade_password(
    void* pool, int64_t user_id, const char* password, size_t password_len, const char* stored_hash)
{
    if (!password || !stored_hash || stored_hash[0] == '\0') {
        return -1;
    }

    if (csilk_bcrypt_verify(password, password_len, stored_hash) == 0) {
        return 0;
    }

    if (strlen(stored_hash) == 61 &&
        mf_legacy_bcrypt_v090_verify(password, password_len, stored_hash) == 0) {
        /* 兼容升级：将 csilk < v1.2.0 遗留算法哈希透明升级为当前 OpenBSD bcrypt 哈希 */
        if (pool && user_id > 0) {
            char upgraded[CSILK_BCRYPT_HASH_LEN];
            csilk_bcrypt_hash(password, password_len, MINEFOLIO_BCRYPT_COST, upgraded);
            mf_auth_repo_update_password((csilk_db_pool_t*)pool, user_id, upgraded);
            OPENSSL_cleanse(upgraded, sizeof(upgraded));
        }
        return 0;
    }

    return -1;
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

static bool
is_registration_allowed(void)
{
    char        reg_cfg[32] = {0};
    const char* v = config_env_get("ENABLE_REGISTRATION", reg_cfg, sizeof(reg_cfg), "1");
    return !v || v[0] == '1' || strcasecmp(v, "true") == 0;
}

static void
seed_new_user_defaults(csilk_db_pool_t* db_pool, int64_t user_id)
{
    int64_t ledger_id = ledger_get_default(db_pool, user_id);
    category_usecase_seed_defaults(db_pool, user_id, ledger_id);
    account_usecase_seed_default(db_pool, user_id, ledger_id);
    mf_import_rule_seed_defaults(db_pool, user_id);
    skill_usecase_seed_defaults(db_pool, user_id);
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
        OPENSSL_cleanse(password_buf, sizeof(password_buf));
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "用户名需≥2字符，密码需≥6字符");
        return -1;
    }

    /* 预先快速校验（避免明显冲突或关闭注册时浪费 bcrypt 计算），权威性校验在事务内复核 */
    mf_user_t check_user = {0};
    if (mf_auth_repo_find_by_username(pool, cmd->username, &check_user) == 0) {
        OPENSSL_cleanse(password_buf, sizeof(password_buf));
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "用户名已存在");
        return -1;
    }

    bool allow_registration = is_registration_allowed();
    int  pre_count = mf_auth_repo_count(pool);
    if (!mf_auth_rule_can_register(pre_count, allow_registration)) {
        OPENSSL_cleanse(password_buf, sizeof(password_buf));
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "当前系统已关闭用户注册");
        return -1;
    }

    char hashed[CSILK_BCRYPT_HASH_LEN];
    store_bcrypt_hash(password, hashed);
    OPENSSL_cleanse(password_buf, sizeof(password_buf));

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    db_tx_scope_t    scope;
    if (db_tx_scope_begin(db_pool, "mf_register_scope", &scope) != 0) {
        OPENSSL_cleanse(hashed, sizeof(hashed));
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    memset(&check_user, 0, sizeof(check_user));
    if (mf_auth_repo_find_by_username(db_pool, cmd->username, &check_user) == 0) {
        OPENSSL_cleanse(hashed, sizeof(hashed));
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "用户名已存在");
        return -1;
    }

    /* 注册开放门禁：首个用户（系统初始化引导）必须放行；已有用户时受 ENABLE_REGISTRATION 控制 */
    int user_count = mf_auth_repo_count(db_pool);
    if (!mf_auth_rule_can_register(user_count, allow_registration)) {
        OPENSSL_cleanse(hashed, sizeof(hashed));
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "当前系统已关闭用户注册");
        return -1;
    }

    const char* initial_role = (user_count == 0) ? "admin" : "user";
    int64_t     user_id = 0;
    if (mf_auth_repo_create_with_role(db_pool, cmd->username, hashed, initial_role, &user_id) !=
            0 ||
        user_id <= 0) {
        OPENSSL_cleanse(hashed, sizeof(hashed));
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "注册失败");
        return -1;
    }
    OPENSSL_cleanse(hashed, sizeof(hashed));

    /* 初始化默认账本，再按账本播种默认分类与导入规则 */
    seed_new_user_defaults(db_pool, user_id);

    if (db_tx_scope_commit(db_pool, &scope) != 0) {
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "提交事务失败");
        return -1;
    }

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

    int         rc = -1;
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
        goto cleanup;
    }

    if (!password) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "缺少用户名或密码");
        goto cleanup;
    }

    mf_user_t user = {0};
    if (mf_auth_repo_find_by_username(pool, cmd->username, &user) != 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "用户名或密码错误");
        goto cleanup;
    }

    if (auth_usecase_verify_and_upgrade_password(
            pool, user.id, password, password_len, user.password_hash) != 0) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "用户名或密码错误");
        goto cleanup;
    }

    if (user.disabled) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "账号已被禁用，请联系管理员");
        goto cleanup;
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
        rc = 0;
        goto cleanup;
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
    rc = 0;

cleanup:
    OPENSSL_cleanse(password_buf, sizeof(password_buf));
    return rc;
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
    if (user.disabled) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权");
        return -1;
    }

    const char*   role = user.role[0] ? user.role : "user";
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", (double)user.id);
    csilk_json_add_string(resp, "username", user.username);
    csilk_json_add_string(resp, "role", role);
    csilk_json_add_bool(resp, "is_admin", strcmp(role, "admin") == 0);
    csilk_json_add_bool(resp, "totp_enabled", user.totp_enabled);
    csilk_json_add_string(resp, "oauth_provider", user.oauth_provider);
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

    (void)c;
    int    rc = -1;
    char   old_pt[512] = {0}, new_pt[512] = {0};
    char   new_hashed[CSILK_BCRYPT_HASH_LEN] = {0};
    size_t old_len = sizeof(old_pt), new_len = sizeof(new_pt);

    if (auth_key_decrypt(cmd->old_password_enc, old_pt, &old_len) != 0 || old_len == 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码解密失败");
        goto cleanup;
    }

    if (auth_key_decrypt(cmd->new_password_enc, new_pt, &new_len) != 0 || new_len == 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "新密码解密失败");
        goto cleanup;
    }

    char err[256];
    if (mf_auth_rule_validate_password((const char*)new_pt, err, sizeof(err)) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "新密码需≥6字符");
        goto cleanup;
    }

    if (strcmp((const char*)old_pt, (const char*)new_pt) == 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "新密码不能与原密码相同");
        goto cleanup;
    }

    mf_user_t check = {0};
    if (mf_auth_repo_get_by_id(pool, cmd->user_id, &check) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码不正确");
        goto cleanup;
    }

    mf_user_t full_user = {0};
    if (mf_auth_repo_find_by_username(pool, check.username, &full_user) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码不正确");
        goto cleanup;
    }

    if (auth_usecase_verify_and_upgrade_password(
            NULL, 0, (const char*)old_pt, old_len, full_user.password_hash) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "原密码不正确");
        goto cleanup;
    }

    store_bcrypt_hash((const char*)new_pt, new_hashed);

    db_tx_scope_t scope;
    if (db_tx_scope_begin((csilk_db_pool_t*)pool, "mf_auth_change_pw", &scope) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库事务开启失败");
        goto cleanup;
    }
    if (mf_auth_repo_update_password(pool, cmd->user_id, new_hashed) != 0 ||
        mf_auth_repo_update_token_version(pool, cmd->user_id) != 0 ||
        db_tx_scope_commit((csilk_db_pool_t*)pool, &scope) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "更新密码失败");
        goto cleanup;
    }

    invalidate_user_jwt(cmd->user_id);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "密码修改成功");
    rc = 0;

cleanup:
    OPENSSL_cleanse(old_pt, sizeof(old_pt));
    OPENSSL_cleanse(new_pt, sizeof(new_pt));
    OPENSSL_cleanse(new_hashed, sizeof(new_hashed));
    return rc;
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
    char clean[32] = {0};
    char digits[32] = {0};
    if (cmd && cmd->code) {
        normalize_auth_code(cmd->code, clean, digits, sizeof(clean));
    }

    if (!pool || !cmd || cmd->user_id <= 0 || strlen(digits) != 6) {
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
    if (!mf_totp_verify_lenient(user.totp_secret, digits, &_ts_unused, NULL)) {
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
    totp_replay_clear_user(cmd->user_id);
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
        if (auth_usecase_verify_and_upgrade_password(
                pool, user.id, pwd, pwd_len, user.password_hash) == 0) {
            authenticated = true;
        }
    }

    /* 2. 或验证当前有效的 6 位 TOTP 动态码 (配置步长容差 + 防同步长重放) */
    if (!authenticated && cmd->code && cmd->code[0] && user.totp_secret[0]) {
        uint64_t _ts_unused;
        int64_t  matched_step = 0;
        if (mf_totp_verify_lenient(user.totp_secret, cmd->code, &_ts_unused, &matched_step) &&
            totp_replay_check_and_record(user.id, matched_step)) {
            authenticated = true;
        }
    }

    OPENSSL_cleanse(pw_buf, sizeof(pw_buf));

    if (!authenticated) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "解绑 2FA 需验证当前密码或有效 6 位动态验证码");
        return -1;
    }

    mf_auth_repo_disable_totp(pool, cmd->user_id);
    mf_auth_repo_update_token_version(pool, cmd->user_id);
    totp_replay_clear_user(cmd->user_id);
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
    if (mf_auth_repo_get_by_id(pool, user_id, &user) != 0 || user.token_version != token_v ||
        user.disabled) {
        out_res->code = 1001;
        snprintf(out_res->message, sizeof(out_res->message), "未授权或临时凭证已失效");
        return -1;
    }

    /* 规范化输入：提取规整的小写 clean_code 与纯数字串 digits_code */
    char clean_code[32] = {0};
    char digits_code[32] = {0};
    normalize_auth_code(cmd->code, clean_code, digits_code, sizeof(clean_code));

    bool     ok = false;
    uint64_t server_ts = (uint64_t)time(NULL);
    int64_t  matched_step = 0;

    /* 1. 若提取出 6 位数字，优先进行 TOTP 动态码校验 (可配置步长容差 + 进程内步长防重放) */
    if (strlen(digits_code) == 6 &&
        mf_totp_verify_lenient(user.totp_secret, digits_code, &server_ts, &matched_step) &&
        totp_replay_check_and_record(user.id, matched_step)) {
        ok = true;
    } else if (user.totp_backup_codes[0] != '\0') {
        char updated_codes[1024];
        /* 2. 依次按原大小写、全小写、全大写，以及带/不带 '-' 变体匹配备用码 */
        char clean_lower[32] = {0};
        char clean_upper[32] = {0};
        for (size_t i = 0; clean_code[i] && i < sizeof(clean_lower) - 1; i++) {
            char ch = clean_code[i];
            clean_lower[i] = (char)((ch >= 'A' && ch <= 'Z') ? (ch + ('a' - 'A')) : ch);
            clean_upper[i] = (char)((ch >= 'a' && ch <= 'z') ? (ch - ('a' - 'A')) : ch);
        }

        const char* try_codes[6];
        int         try_count = 0;
        try_codes[try_count++] = clean_code;
        if (strcmp(clean_lower, clean_code) != 0) {
            try_codes[try_count++] = clean_lower;
        }
        if (strcmp(clean_upper, clean_code) != 0) {
            try_codes[try_count++] = clean_upper;
        }

        char   alt_code[32] = {0};
        size_t clen = strlen(clean_code);
        if (clen == 9 && clean_code[4] == '-') {
            memcpy(alt_code, clean_code, 4);
            memcpy(alt_code + 4, clean_code + 5, 4);
            alt_code[8] = '\0';
        } else if (clen == 8) {
            memcpy(alt_code, clean_code, 4);
            alt_code[4] = '-';
            memcpy(alt_code + 5, clean_code + 4, 4);
            alt_code[9] = '\0';
        }
        char alt_lower[32] = {0};
        char alt_upper[32] = {0};
        if (alt_code[0]) {
            for (size_t i = 0; alt_code[i] && i < sizeof(alt_lower) - 1; i++) {
                char ch = alt_code[i];
                alt_lower[i] = (char)((ch >= 'A' && ch <= 'Z') ? (ch + ('a' - 'A')) : ch);
                alt_upper[i] = (char)((ch >= 'a' && ch <= 'z') ? (ch - ('a' - 'A')) : ch);
            }
            try_codes[try_count++] = alt_code;
            if (strcmp(alt_lower, alt_code) != 0) {
                try_codes[try_count++] = alt_lower;
            }
            if (strcmp(alt_upper, alt_code) != 0) {
                try_codes[try_count++] = alt_upper;
            }
        }

        for (int i = 0; i < try_count; i++) {
            if (csilk_totp_verify_and_consume_backup_code(
                    user.totp_backup_codes, try_codes[i], updated_codes, sizeof(updated_codes))) {
                mf_auth_repo_update_backup_codes(pool, user_id, updated_codes);
                ok = true;
                break;
            }
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

    char          oidc_client_id[256] = {0};
    char          oidc_auth_url[512] = {0};
    char          oidc_token_url[512] = {0};
    char          oidc_redirect_uri[512] = {0};
    const char*   has_id = config_secret_get("OAUTH_OIDC_CLIENT_ID", oidc_client_id, sizeof(oidc_client_id));
    const char*   has_auth = config_env_get("OAUTH_OIDC_AUTH_URL", oidc_auth_url, sizeof(oidc_auth_url), NULL);
    const char*   has_tok = config_env_get("OAUTH_OIDC_TOKEN_URL", oidc_token_url, sizeof(oidc_token_url), NULL);
    const char*   has_redir =
        config_env_get("OAUTH_OIDC_REDIRECT_URI", oidc_redirect_uri, sizeof(oidc_redirect_uri), NULL);
    if (has_id && oidc_client_id[0] && has_auth && oidc_auth_url[0] && has_tok &&
        oidc_token_url[0]) {
        csilk_json_t* oidc = csilk_json_object();
        csilk_json_add_string(oidc, "id", "oidc");
        csilk_json_add_string(oidc, "name", "Single Sign-On (OIDC)");
        csilk_json_add_string(oidc, "icon", "ph:key");
        char full_auth_url[1536];
        if (has_redir && oidc_redirect_uri[0]) {
            CURL* esc_curl = curl_easy_init();
            char* enc_redir =
                esc_curl ? curl_easy_escape(esc_curl, oidc_redirect_uri, (int)strlen(oidc_redirect_uri))
                         : NULL;
            snprintf(full_auth_url,
                     sizeof(full_auth_url),
                     "%s?client_id=%s&redirect_uri=%s&response_type=code&scope=openid%%20profile%%20email",
                     oidc_auth_url,
                     oidc_client_id,
                     enc_redir ? enc_redir : oidc_redirect_uri);
            if (enc_redir) {
                curl_free(enc_redir);
            }
            if (esc_curl) {
                curl_easy_cleanup(esc_curl);
            }
        } else {
            snprintf(full_auth_url,
                     sizeof(full_auth_url),
                     "%s?client_id=%s&response_type=code&scope=openid%%20profile%%20email",
                     oidc_auth_url,
                     oidc_client_id);
        }
        csilk_json_add_string(oidc, "auth_url", full_auth_url);
        csilk_json_array_append(providers, oidc);
    }
    OPENSSL_cleanse(oidc_client_id, sizeof(oidc_client_id));

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
    static const size_t MAX_OAUTH_RESP = 1024U * 1024U;
    oauth_curl_buf_t*   b = (oauth_curl_buf_t*)userdata;
    if (!b || !ptr) {
        return 0;
    }
    if (size != 0 && nmemb > SIZE_MAX / size) {
        return 0;
    }
    size_t total = size * nmemb;
    if (total > MAX_OAUTH_RESP || b->size > MAX_OAUTH_RESP - total) {
        return 0;
    }
    if (b->size + total + 1 > b->cap) {
        size_t new_cap = (b->cap + total + 1) * 2;
        if (new_cap > MAX_OAUTH_RESP + 1) {
            new_cap = MAX_OAUTH_RESP + 1;
        }
        char* nd = realloc(b->data, new_cap);
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

    char* enc_code = curl_easy_escape(curl, code, (int)strlen(code));
    if (!enc_code) {
        curl_easy_cleanup(curl);
        return -1;
    }

    char post_fields[1024];
    int  pf_n = snprintf(post_fields,
                         sizeof(post_fields),
                         "client_id=%s&client_secret=%s&code=%s",
                         client_id,
                         client_secret,
                         enc_code);
    curl_free(enc_code);
    if (pf_n < 0 || (size_t)pf_n >= sizeof(post_fields)) {
        OPENSSL_cleanse(post_fields, sizeof(post_fields));
        curl_easy_cleanup(curl);
        return -1;
    }

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
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    OPENSSL_cleanse(post_fields, sizeof(post_fields));
    long http_code = 0;
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
    OPENSSL_cleanse(auth_hdr, sizeof(auth_hdr));
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
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

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

static int
oidc_decode_b64url_alloc(const char* src, uint8_t** out_buf, size_t* out_len)
{
    if (!src || !src[0] || !out_buf || !out_len) {
        return -1;
    }
    size_t slen = strlen(src);
    size_t cap = (slen * 3U) / 4U + 16U;
    uint8_t* buf = (uint8_t*)calloc(1, cap + 1U);
    if (!buf) {
        return -1;
    }
    int n = csilk_base64url_decode(src, buf, cap);
    if (n < 0) {
        free(buf);
        return -1;
    }
    buf[n] = '\0';
    *out_buf = buf;
    *out_len = (size_t)n;
    return 0;
}

static EVP_PKEY*
oidc_build_rsa_pkey_from_jwk(const csilk_json_t* jwk)
{
    const char* n_b64 = csilk_json_get_string(jwk, "n");
    const char* e_b64 = csilk_json_get_string(jwk, "e");
    if (!n_b64 || !e_b64) {
        return NULL;
    }
    uint8_t* n_raw = NULL;
    uint8_t* e_raw = NULL;
    size_t   n_len = 0, e_len = 0;
    if (oidc_decode_b64url_alloc(n_b64, &n_raw, &n_len) != 0 ||
        oidc_decode_b64url_alloc(e_b64, &e_raw, &e_len) != 0 || n_len == 0 || e_len == 0) {
        free(n_raw);
        free(e_raw);
        return NULL;
    }

    BIGNUM*         bn_n = BN_bin2bn(n_raw, (int)n_len, NULL);
    BIGNUM*         bn_e = BN_bin2bn(e_raw, (int)e_len, NULL);
    OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
    OSSL_PARAM*     params = NULL;
    EVP_PKEY_CTX*   pctx = NULL;
    EVP_PKEY*       pkey = NULL;

    if (bn_n && bn_e && bld && OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn_n) &&
        OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, bn_e)) {
        params = OSSL_PARAM_BLD_to_param(bld);
        pctx = params ? EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL) : NULL;
        if (pctx && EVP_PKEY_fromdata_init(pctx) > 0) {
            if (EVP_PKEY_fromdata(pctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0) {
                pkey = NULL;
            }
        }
    }

    EVP_PKEY_CTX_free(pctx);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    BN_free(bn_n);
    BN_free(bn_e);
    free(n_raw);
    free(e_raw);
    return pkey;
}

static EVP_PKEY*
oidc_build_ec_p256_pkey_from_jwk(const csilk_json_t* jwk)
{
    const char* crv = csilk_json_get_string(jwk, "crv");
    const char* x_b64 = csilk_json_get_string(jwk, "x");
    const char* y_b64 = csilk_json_get_string(jwk, "y");
    if (!crv || strcmp(crv, "P-256") != 0 || !x_b64 || !y_b64) {
        return NULL;
    }
    uint8_t* x_raw = NULL;
    uint8_t* y_raw = NULL;
    size_t   x_len = 0, y_len = 0;
    if (oidc_decode_b64url_alloc(x_b64, &x_raw, &x_len) != 0 ||
        oidc_decode_b64url_alloc(y_b64, &y_raw, &y_len) != 0 || x_len == 0 || x_len > 32 ||
        y_len == 0 || y_len > 32) {
        free(x_raw);
        free(y_raw);
        return NULL;
    }

    uint8_t uncompressed[65];
    memset(uncompressed, 0, sizeof(uncompressed));
    uncompressed[0] = 0x04;
    memcpy(uncompressed + 1 + (32 - x_len), x_raw, x_len);
    memcpy(uncompressed + 33 + (32 - y_len), y_raw, y_len);
    free(x_raw);
    free(y_raw);

    OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
    OSSL_PARAM*     params = NULL;
    EVP_PKEY_CTX*   pctx = NULL;
    EVP_PKEY*       pkey = NULL;

    if (bld &&
        OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0) &&
        OSSL_PARAM_BLD_push_octet_string(
            bld, OSSL_PKEY_PARAM_PUB_KEY, uncompressed, sizeof(uncompressed))) {
        params = OSSL_PARAM_BLD_to_param(bld);
        pctx = params ? EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL) : NULL;
        if (pctx && EVP_PKEY_fromdata_init(pctx) > 0) {
            if (EVP_PKEY_fromdata(pctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0) {
                pkey = NULL;
            }
        }
    }

    EVP_PKEY_CTX_free(pctx);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    return pkey;
}

static int
oidc_verify_jws_signature(const char* signing_input,
                          size_t      signing_len,
                          const char* sig_b64,
                          const char* alg,
                          const char* kid,
                          const char* jwks_json)
{
    bool is_rs256 = (strcmp(alg, "RS256") == 0);
    bool is_es256 = (strcmp(alg, "ES256") == 0);
    if (!is_rs256 && !is_es256) {
        return -1;
    }

    csilk_json_t* jwks = csilk_json_parse(jwks_json);
    if (!jwks) {
        return -1;
    }
    const csilk_json_t* keys = csilk_json_get(jwks, "keys");
    if (!keys || !csilk_json_is_array(keys)) {
        csilk_json_free(jwks);
        return -1;
    }

    uint8_t* sig_raw = NULL;
    size_t   sig_raw_len = 0;
    if (oidc_decode_b64url_alloc(sig_b64, &sig_raw, &sig_raw_len) != 0 || sig_raw_len == 0) {
        free(sig_raw);
        csilk_json_free(jwks);
        return -1;
    }

    uint8_t*       verify_sig = sig_raw;
    size_t         verify_sig_len = sig_raw_len;
    unsigned char* der_sig = NULL;

    if (is_es256) {
        if (sig_raw_len != 64) {
            free(sig_raw);
            csilk_json_free(jwks);
            return -1;
        }
        ECDSA_SIG* esig = ECDSA_SIG_new();
        BIGNUM*    r = BN_bin2bn(sig_raw, 32, NULL);
        BIGNUM*    s = BN_bin2bn(sig_raw + 32, 32, NULL);
        if (!esig || !r || !s || ECDSA_SIG_set0(esig, r, s) != 1) {
            if (!esig || ECDSA_SIG_set0(esig, NULL, NULL) == 0) {
                BN_free(r);
                BN_free(s);
            }
            ECDSA_SIG_free(esig);
            free(sig_raw);
            csilk_json_free(jwks);
            return -1;
        }
        int der_len = i2d_ECDSA_SIG(esig, &der_sig);
        ECDSA_SIG_free(esig);
        if (der_len <= 0 || !der_sig) {
            OPENSSL_free(der_sig);
            free(sig_raw);
            csilk_json_free(jwks);
            return -1;
        }
        verify_sig = der_sig;
        verify_sig_len = (size_t)der_len;
    }

    int    verified = -1;
    size_t key_cnt = csilk_json_array_size(keys);
    for (size_t i = 0; i < key_cnt; i++) {
        const csilk_json_t* jwk = csilk_json_array_get(keys, i);
        if (!jwk) {
            continue;
        }
        const char* k_use = csilk_json_get_string(jwk, "use");
        if (k_use && k_use[0] && strcmp(k_use, "sig") != 0) {
            continue;
        }
        const char* k_kid = csilk_json_get_string(jwk, "kid");
        if (kid && kid[0] && (!k_kid || strcmp(k_kid, kid) != 0)) {
            continue;
        }
        const char* k_alg = csilk_json_get_string(jwk, "alg");
        if (k_alg && k_alg[0] && strcmp(k_alg, alg) != 0) {
            continue;
        }
        const char* kty = csilk_json_get_string(jwk, "kty");
        if (!kty) {
            continue;
        }

        EVP_PKEY* pkey = NULL;
        if (is_rs256 && strcmp(kty, "RSA") == 0) {
            pkey = oidc_build_rsa_pkey_from_jwk(jwk);
        } else if (is_es256 && strcmp(kty, "EC") == 0) {
            pkey = oidc_build_ec_p256_pkey_from_jwk(jwk);
        }
        if (!pkey) {
            continue;
        }

        EVP_MD_CTX* mctx = EVP_MD_CTX_new();
        if (mctx &&
            EVP_DigestVerifyInit(mctx, NULL, EVP_sha256(), NULL, pkey) == 1 &&
            EVP_DigestVerify(mctx,
                             verify_sig,
                             verify_sig_len,
                             (const unsigned char*)signing_input,
                             signing_len) == 1) {
            verified = 0;
        }
        EVP_MD_CTX_free(mctx);
        EVP_PKEY_free(pkey);
        if (verified == 0) {
            break;
        }
    }

    if (der_sig) {
        OPENSSL_free(der_sig);
    }
    free(sig_raw);
    csilk_json_free(jwks);
    return verified;
}

int
auth_oidc_verify_id_token(const char* id_token,
                          const char* expected_client_id,
                          const char* expected_issuer,
                          const char* jwks_json,
                          int64_t     now_ts,
                          char*       out_sub,
                          size_t      sub_cap,
                          char*       out_username,
                          size_t      un_cap)
{
    if (!id_token || !id_token[0] || !out_sub || sub_cap == 0) {
        return -1;
    }
    out_sub[0] = '\0';
    if (out_username && un_cap > 0) {
        out_username[0] = '\0';
    }

    const char* dot1 = strchr(id_token, '.');
    if (!dot1 || dot1 == id_token) {
        return -1;
    }
    const char* dot2 = strchr(dot1 + 1, '.');
    if (!dot2 || dot2 == dot1 + 1 || !dot2[1] || strchr(dot2 + 1, '.') != NULL) {
        return -1;
    }

    size_t hdr_len = (size_t)(dot1 - id_token);
    size_t pay_len = (size_t)(dot2 - (dot1 + 1));
    char*  hdr_b64 = (char*)malloc(hdr_len + 1);
    char*  pay_b64 = (char*)malloc(pay_len + 1);
    if (!hdr_b64 || !pay_b64) {
        free(hdr_b64);
        free(pay_b64);
        return -1;
    }
    memcpy(hdr_b64, id_token, hdr_len);
    hdr_b64[hdr_len] = '\0';
    memcpy(pay_b64, dot1 + 1, pay_len);
    pay_b64[pay_len] = '\0';

    uint8_t* hdr_raw = NULL;
    uint8_t* pay_raw = NULL;
    size_t   hdr_raw_len = 0, pay_raw_len = 0;
    int      dec_rc = oidc_decode_b64url_alloc(hdr_b64, &hdr_raw, &hdr_raw_len);
    int      dec_rc2 = oidc_decode_b64url_alloc(pay_b64, &pay_raw, &pay_raw_len);
    free(hdr_b64);
    free(pay_b64);
    if (dec_rc != 0 || dec_rc2 != 0) {
        free(hdr_raw);
        free(pay_raw);
        return -1;
    }

    csilk_json_t* hdr = csilk_json_parse((const char*)hdr_raw);
    csilk_json_t* pay = csilk_json_parse((const char*)pay_raw);
    free(hdr_raw);
    free(pay_raw);
    if (!hdr || !pay) {
        if (hdr) {
            csilk_json_free(hdr);
        }
        if (pay) {
            csilk_json_free(pay);
        }
        return -1;
    }

    const char* alg = csilk_json_get_string(hdr, "alg");
    const char* kid = csilk_json_get_string(hdr, "kid");
    if (!alg || !alg[0] || strcasecmp(alg, "none") == 0) {
        csilk_json_free(hdr);
        csilk_json_free(pay);
        return -1;
    }

    if (jwks_json && jwks_json[0]) {
        if (oidc_verify_jws_signature(
                id_token, (size_t)(dot2 - id_token), dot2 + 1, alg, kid, jwks_json) != 0) {
            csilk_json_free(hdr);
            csilk_json_free(pay);
            return -1;
        }
    }

    const char* sub = csilk_json_get_string(pay, "sub");
    if (!sub || !sub[0]) {
        csilk_json_free(hdr);
        csilk_json_free(pay);
        return -1;
    }

    const csilk_json_t* exp_v = csilk_json_get(pay, "exp");
    if (!exp_v || !csilk_json_is_number(exp_v)) {
        csilk_json_free(hdr);
        csilk_json_free(pay);
        return -1;
    }
    int64_t exp_ts = (int64_t)csilk_json_number_value(exp_v);
    int64_t eff_now = (now_ts > 0) ? now_ts : (int64_t)time(NULL);
    if (exp_ts + 60 < eff_now) {
        csilk_json_free(hdr);
        csilk_json_free(pay);
        return -1;
    }

    if (expected_issuer && expected_issuer[0]) {
        const char* iss = csilk_json_get_string(pay, "iss");
        if (!iss || strcmp(iss, expected_issuer) != 0) {
            csilk_json_free(hdr);
            csilk_json_free(pay);
            return -1;
        }
    }

    if (expected_client_id && expected_client_id[0]) {
        const csilk_json_t* aud_v = csilk_json_get(pay, "aud");
        bool                aud_ok = false;
        size_t              aud_cnt = 0;
        if (aud_v && csilk_json_is_string(aud_v)) {
            const char* a = csilk_json_string_value(aud_v);
            aud_ok = (a && strcmp(a, expected_client_id) == 0);
            aud_cnt = 1;
        } else if (aud_v && csilk_json_is_array(aud_v)) {
            aud_cnt = csilk_json_array_size(aud_v);
            for (size_t i = 0; i < aud_cnt; i++) {
                const csilk_json_t* elem = csilk_json_array_get(aud_v, i);
                if (elem && csilk_json_is_string(elem)) {
                    const char* a = csilk_json_string_value(elem);
                    if (a && strcmp(a, expected_client_id) == 0) {
                        aud_ok = true;
                        break;
                    }
                }
            }
        }
        if (!aud_ok) {
            csilk_json_free(hdr);
            csilk_json_free(pay);
            return -1;
        }
        if (aud_cnt > 1) {
            const char* azp = csilk_json_get_string(pay, "azp");
            if (azp && azp[0] && strcmp(azp, expected_client_id) != 0) {
                csilk_json_free(hdr);
                csilk_json_free(pay);
                return -1;
            }
        }
    }

    snprintf(out_sub, sub_cap, "%s", sub);
    if (out_username && un_cap > 0) {
        const char* pref = csilk_json_get_string(pay, "preferred_username");
        const char* email = csilk_json_get_string(pay, "email");
        const char* name = csilk_json_get_string(pay, "name");
        const char* pick = (pref && pref[0]) ? pref
                                              : ((email && email[0]) ? email
                                                                     : ((name && name[0]) ? name : sub));
        snprintf(out_username, un_cap, "%s", pick);
    }

    csilk_json_free(hdr);
    csilk_json_free(pay);
    return 0;
}

static int
oidc_http_request(const char*        url,
                  const char*        post_body,
                  struct curl_slist* extra_headers,
                  bool               allow_local,
                  char**             out_body)
{
    if (!url || !url[0] || !out_body) {
        return -1;
    }
    *out_body = NULL;

    char resolve_entry[320] = {0};
    if (mf_http_validate_and_resolve_url(
            url, 0, allow_local, false, resolve_entry, sizeof(resolve_entry)) != 0) {
        return -1;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    struct curl_slist* resolve_list =
        mf_http_curl_apply_security(curl, resolve_entry[0] ? resolve_entry : NULL, NULL, false);

    mf_curl_buf_t buf = {.data = (char*)malloc(8192), .size = 0, .cap = 8192, .max_bytes = MF_CURL_MAX_1MB, .overflow = false};
    if (!buf.data) {
        curl_slist_free_all(resolve_list);
        curl_easy_cleanup(curl);
        return -1;
    }
    buf.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mf_curl_bounded_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if (extra_headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, extra_headers);
    }
    if (post_body) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body);
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    CURLcode res = curl_easy_perform(curl);
    long     http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(resolve_list);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || buf.overflow || http_code != 200 || buf.size == 0) {
        free(buf.data);
        return -1;
    }

    *out_body = buf.data;
    return 0;
}

static int
oauth_exchange_oidc(
    const char* code, char* out_oauth_id, size_t id_cap, char* out_username, size_t un_cap)
{
    char client_id[256] = {0};
    char client_secret[512] = {0};
    char token_url[512] = {0};
    char userinfo_url[512] = {0};
    char jwks_url[512] = {0};
    char issuer[512] = {0};
    char redirect_uri[512] = {0};
    char allow_local_buf[16] = {0};

    if (!config_secret_get("OAUTH_OIDC_CLIENT_ID", client_id, sizeof(client_id)) ||
        !client_id[0] ||
        !config_secret_get("OAUTH_OIDC_CLIENT_SECRET", client_secret, sizeof(client_secret)) ||
        !client_secret[0] ||
        !config_env_get("OAUTH_OIDC_TOKEN_URL", token_url, sizeof(token_url), NULL) ||
        !token_url[0]) {
        OPENSSL_cleanse(client_id, sizeof(client_id));
        OPENSSL_cleanse(client_secret, sizeof(client_secret));
        return -2;
    }
    config_env_get("OAUTH_OIDC_USERINFO_URL", userinfo_url, sizeof(userinfo_url), NULL);
    config_env_get("OAUTH_OIDC_JWKS_URL", jwks_url, sizeof(jwks_url), NULL);
    config_env_get("OAUTH_OIDC_ISSUER", issuer, sizeof(issuer), NULL);
    config_env_get("OAUTH_OIDC_REDIRECT_URI", redirect_uri, sizeof(redirect_uri), NULL);
    config_env_get("OAUTH_OIDC_ALLOW_LOCAL", allow_local_buf, sizeof(allow_local_buf), "0");
    bool allow_local =
        (strcmp(allow_local_buf, "1") == 0 || strcasecmp(allow_local_buf, "true") == 0);

    CURL* esc = curl_easy_init();
    if (!esc) {
        OPENSSL_cleanse(client_id, sizeof(client_id));
        OPENSSL_cleanse(client_secret, sizeof(client_secret));
        return -1;
    }
    char* enc_code = curl_easy_escape(esc, code, (int)strlen(code));
    char* enc_cid = curl_easy_escape(esc, client_id, (int)strlen(client_id));
    char* enc_csec = curl_easy_escape(esc, client_secret, (int)strlen(client_secret));
    char* enc_redir =
        redirect_uri[0] ? curl_easy_escape(esc, redirect_uri, (int)strlen(redirect_uri)) : NULL;

    char post_fields[2048];
    int  pf_n = -1;
    if (enc_code && enc_cid && enc_csec) {
        if (enc_redir) {
            pf_n = snprintf(post_fields,
                            sizeof(post_fields),
                            "grant_type=authorization_code&code=%s&client_id=%s&client_secret=%s&"
                            "redirect_uri=%s",
                            enc_code,
                            enc_cid,
                            enc_csec,
                            enc_redir);
        } else {
            pf_n = snprintf(post_fields,
                            sizeof(post_fields),
                            "grant_type=authorization_code&code=%s&client_id=%s&client_secret=%s",
                            enc_code,
                            enc_cid,
                            enc_csec);
        }
    }
    if (enc_code) {
        curl_free(enc_code);
    }
    if (enc_cid) {
        curl_free(enc_cid);
    }
    if (enc_csec) {
        OPENSSL_cleanse(enc_csec, strlen(enc_csec));
        curl_free(enc_csec);
    }
    if (enc_redir) {
        curl_free(enc_redir);
    }
    curl_easy_cleanup(esc);
    OPENSSL_cleanse(client_secret, sizeof(client_secret));

    if (pf_n < 0 || (size_t)pf_n >= sizeof(post_fields)) {
        OPENSSL_cleanse(post_fields, sizeof(post_fields));
        OPENSSL_cleanse(client_id, sizeof(client_id));
        return -1;
    }

    struct curl_slist* tok_hdrs = NULL;
    tok_hdrs = curl_slist_append(tok_hdrs, "Accept: application/json");
    tok_hdrs = curl_slist_append(tok_hdrs, "Content-Type: application/x-www-form-urlencoded");
    tok_hdrs = curl_slist_append(tok_hdrs, "User-Agent: Minefolio-Backend");

    char* tok_body = NULL;
    int   http_rc = oidc_http_request(token_url, post_fields, tok_hdrs, allow_local, &tok_body);
    OPENSSL_cleanse(post_fields, sizeof(post_fields));
    curl_slist_free_all(tok_hdrs);
    if (http_rc != 0 || !tok_body) {
        OPENSSL_cleanse(client_id, sizeof(client_id));
        return -1;
    }

    csilk_json_t* tok_resp = csilk_json_parse(tok_body);
    OPENSSL_cleanse(tok_body, strlen(tok_body));
    free(tok_body);
    if (!tok_resp) {
        OPENSSL_cleanse(client_id, sizeof(client_id));
        return -1;
    }

    const char* access_tok = csilk_json_get_string(tok_resp, "access_token");
    const char* id_token = csilk_json_get_string(tok_resp, "id_token");

    bool has_id_sub = false;
    char id_sub[128] = {0};
    char id_user[128] = {0};

    if (id_token && id_token[0]) {
        char* jwks_body = NULL;
        if (jwks_url[0]) {
            if (oidc_http_request(jwks_url, NULL, NULL, allow_local, &jwks_body) != 0 ||
                !jwks_body) {
                csilk_json_free(tok_resp);
                OPENSSL_cleanse(client_id, sizeof(client_id));
                return -1;
            }
        }
        int vrc = auth_oidc_verify_id_token(id_token,
                                            client_id,
                                            issuer[0] ? issuer : NULL,
                                            jwks_body,
                                            0,
                                            id_sub,
                                            sizeof(id_sub),
                                            id_user,
                                            sizeof(id_user));
        free(jwks_body);
        if (vrc != 0) {
            csilk_json_free(tok_resp);
            OPENSSL_cleanse(client_id, sizeof(client_id));
            return -1;
        }
        has_id_sub = true;
    }
    OPENSSL_cleanse(client_id, sizeof(client_id));

    bool has_ui_sub = false;
    char ui_sub[128] = {0};
    char ui_user[128] = {0};

    if (userinfo_url[0] && access_tok && access_tok[0]) {
        char auth_hdr[1024];
        int  ahn = snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", access_tok);
        if (ahn < 0 || (size_t)ahn >= sizeof(auth_hdr)) {
            OPENSSL_cleanse(auth_hdr, sizeof(auth_hdr));
            csilk_json_free(tok_resp);
            return -1;
        }
        struct curl_slist* ui_hdrs = NULL;
        ui_hdrs = curl_slist_append(ui_hdrs, auth_hdr);
        OPENSSL_cleanse(auth_hdr, sizeof(auth_hdr));
        ui_hdrs = curl_slist_append(ui_hdrs, "Accept: application/json");
        ui_hdrs = curl_slist_append(ui_hdrs, "User-Agent: Minefolio-Backend");

        char* ui_body = NULL;
        int   ui_rc = oidc_http_request(userinfo_url, NULL, ui_hdrs, allow_local, &ui_body);
        curl_slist_free_all(ui_hdrs);
        if (ui_rc == 0 && ui_body) {
            csilk_json_t* ui_resp = csilk_json_parse(ui_body);
            free(ui_body);
            if (ui_resp) {
                const char* sub = csilk_json_get_string(ui_resp, "sub");
                const char* pref = csilk_json_get_string(ui_resp, "preferred_username");
                const char* email = csilk_json_get_string(ui_resp, "email");
                const char* name = csilk_json_get_string(ui_resp, "name");
                if (sub && sub[0]) {
                    snprintf(ui_sub, sizeof(ui_sub), "%s", sub);
                    const char* pick =
                        (pref && pref[0]) ? pref
                                          : ((email && email[0]) ? email
                                                                 : ((name && name[0]) ? name : sub));
                    snprintf(ui_user, sizeof(ui_user), "%s", pick);
                    has_ui_sub = true;
                }
                csilk_json_free(ui_resp);
            }
        } else {
            free(ui_body);
        }
    }
    csilk_json_free(tok_resp);

    if (has_id_sub && has_ui_sub && strcmp(id_sub, ui_sub) != 0) {
        /* Prevent sub-substitution mismatch between ID Token and UserInfo */
        return -1;
    }

    if (has_ui_sub) {
        snprintf(out_oauth_id, id_cap, "%s", ui_sub);
        snprintf(out_username, un_cap, "%s", ui_user[0] ? ui_user : ui_sub);
        return 0;
    }
    if (has_id_sub) {
        snprintf(out_oauth_id, id_cap, "%s", id_sub);
        snprintf(out_username, un_cap, "%s", id_user[0] ? id_user : id_sub);
        return 0;
    }
    return -1;
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
        } else if (strcmp(provider_buf, "oidc") == 0) {
            int ex_rc = oauth_exchange_oidc(
                cmd->code, oauth_id_buf, sizeof(oauth_id_buf), username_buf, sizeof(username_buf));
            if (ex_rc == -2) {
                out_res->code = 1002;
                snprintf(
                    out_res->message, sizeof(out_res->message), "OIDC OAuth 服务端配置未就绪");
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
        if (oauth_user.disabled) {
            out_res->code = 1004;
            snprintf(out_res->message, sizeof(out_res->message), "账号已被禁用，请联系管理员");
            return -1;
        }
        int is_2fa_active =
            oauth_user.totp_enabled || ((oauth_user.totp_secret[0] != '\0') &&
                                        (oauth_user.totp_backup_codes[0] != '\0' &&
                                         strcmp(oauth_user.totp_backup_codes, "[]") != 0));
        if (is_2fa_active) {
            char* temp_token =
                jwt_generate_temp_token(NULL, oauth_user.id, (int)oauth_user.token_version, 300);
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
        user_id = oauth_user.id;
        token_version = (int)oauth_user.token_version;
        snprintf(final_username, sizeof(final_username), "%s", oauth_user.username);
    } else {
        if (!is_registration_allowed()) {
            out_res->code = 1004;
            snprintf(out_res->message, sizeof(out_res->message), "当前系统已关闭用户注册");
            return -1;
        }
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

int
auth_usecase_is_instance_admin(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return 0;
    }
    return mf_auth_repo_is_instance_admin((csilk_db_pool_t*)pool, user_id);
}

int64_t
auth_usecase_get_instance_admin_id(void* pool)
{
    if (!pool) {
        return -1;
    }
    return mf_auth_repo_get_instance_admin_id((csilk_db_pool_t*)pool);
}

int
auth_usecase_user_count(void* pool)
{
    if (!pool) {
        return 0;
    }
    return mf_auth_repo_count((csilk_db_pool_t*)pool);
}

int
auth_usecase_setup_first_admin(void*                  pool,
                               csilk_ctx_t*           c,
                               const char*            username,
                               const char*            password_enc,
                               csilk_json_t**         out_data,
                               auth_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !out_data) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "服务器内部错误");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    if (mf_auth_repo_count(db_pool) > 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "系统已完成初始化，禁止重复设置");
        return -1;
    }

    if (!username || !password_enc || strlen(username) < 2) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "用户名需≥2字符");
        return -1;
    }

    uint8_t pt_buf[512];
    size_t  pt_len = sizeof(pt_buf);
    uint8_t ct_buf[CSILK_RSA_KEY_SIZE];
    if (csilk_base64url_decode(password_enc, ct_buf, sizeof(ct_buf)) < 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "密码格式错误");
        return -1;
    }
    if (_csilk_asymmetric_decrypt(c,
                                  auth_key_get_private_pem(),
                                  strlen(auth_key_get_private_pem()),
                                  ct_buf,
                                  CSILK_RSA_KEY_SIZE,
                                  pt_buf,
                                  &pt_len) != 0 ||
        pt_len == 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "密码解密失败");
        return -1;
    }
    if (pt_len < 6) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "密码需≥6字符");
        return -1;
    }
    pt_buf[pt_len] = '\0';
    const char* password = (const char*)pt_buf;

    char hashed[CSILK_BCRYPT_HASH_LEN];
    csilk_bcrypt_hash(password, strlen(password), CSILK_BCRYPT_DEFAULT_COST, hashed);
    OPENSSL_cleanse(pt_buf, sizeof(pt_buf));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(db_pool, "mf_setup_scope", &scope) != 0) {
        OPENSSL_cleanse(hashed, sizeof(hashed));
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    if (mf_auth_repo_count(db_pool) > 0) {
        OPENSSL_cleanse(hashed, sizeof(hashed));
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "系统已完成初始化，禁止重复设置");
        return -1;
    }

    int64_t user_id = 0;
    if (mf_auth_repo_create_with_role(db_pool, username, hashed, "admin", &user_id) != 0 ||
        user_id <= 0) {
        OPENSSL_cleanse(hashed, sizeof(hashed));
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "初始化失败");
        return -1;
    }
    OPENSSL_cleanse(hashed, sizeof(hashed));

    seed_new_user_defaults(db_pool, user_id);

    if (db_tx_scope_commit(db_pool, &scope) != 0) {
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "提交事务失败");
        return -1;
    }

    char*         token = jwt_generate_token(c, user_id, 0);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "token", token ? token : "");
    csilk_json_add_number(resp, "expires_in", 604800);
    free(token);

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

static csilk_json_t*
user_to_summary_json(const mf_user_t* u)
{
    const char*   role = (u->role[0]) ? u->role : "user";
    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_number(obj, "id", (double)u->id);
    csilk_json_add_string(obj, "username", u->username);
    csilk_json_add_string(obj, "role", role);
    csilk_json_add_bool(obj, "is_admin", strcmp(role, "admin") == 0);
    csilk_json_add_bool(obj, "disabled", u->disabled);
    csilk_json_add_bool(obj, "totp_enabled", u->totp_enabled);
    csilk_json_add_string(obj, "oauth_provider", u->oauth_provider);
    csilk_json_add_string(obj, "created_at", u->created_at);
    return obj;
}

static csilk_json_t*
user_summary_to_json(const mf_user_summary_t* u)
{
    const char*   role = (u->role[0]) ? u->role : "user";
    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_number(obj, "id", (double)u->id);
    csilk_json_add_string(obj, "username", u->username);
    csilk_json_add_string(obj, "role", role);
    csilk_json_add_bool(obj, "is_admin", strcmp(role, "admin") == 0);
    csilk_json_add_bool(obj, "disabled", u->disabled);
    csilk_json_add_bool(obj, "totp_enabled", u->totp_enabled);
    csilk_json_add_string(obj, "oauth_provider", u->oauth_provider);
    csilk_json_add_string(obj, "created_at", u->created_at);
    return obj;
}

int
auth_usecase_admin_list_users(void*                         pool,
                              const admin_list_users_cmd_t* cmd,
                              csilk_json_t**                out_data,
                              auth_usecase_result_t*        out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !out_data) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "服务器内部错误");
        return -1;
    }

    int64_t     page = (cmd && cmd->page > 0) ? cmd->page : 1;
    int64_t     page_size = (cmd && cmd->page_size > 0) ? cmd->page_size : 20;
    const char* keyword = cmd ? cmd->keyword : NULL;
    const char* role = cmd ? cmd->role : NULL;
    int         disabled_filter = -1;
    if (cmd && cmd->status) {
        if (strcmp(cmd->status, "active") == 0) {
            disabled_filter = 0;
        } else if (strcmp(cmd->status, "disabled") == 0) {
            disabled_filter = 1;
        }
    }

    mf_user_summary_t* items = NULL;
    int                count = 0;
    int64_t            total = 0;
    if (mf_auth_repo_list_users(
            pool, page, page_size, keyword, role, disabled_filter, &items, &count, &total) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询用户列表失败");
        return -1;
    }

    mf_user_stats_t stats = {0};
    mf_auth_repo_get_user_stats(pool, &stats);

    csilk_json_t* list = csilk_json_array();
    for (int i = 0; i < count; i++) {
        csilk_json_array_append(list, user_summary_to_json(&items[i]));
    }
    free(items);

    csilk_json_t* stats_obj = csilk_json_object();
    csilk_json_add_number(stats_obj, "total_users", (double)stats.total_users);
    csilk_json_add_number(stats_obj, "active_users", (double)stats.active_users);
    csilk_json_add_number(stats_obj, "admin_users", (double)stats.admin_users);
    csilk_json_add_number(stats_obj, "active_admin_users", (double)stats.active_admin_users);
    csilk_json_add_number(stats_obj, "disabled_users", (double)stats.disabled_users);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_array(resp, "list", list);
    csilk_json_add_number(resp, "total", (double)total);
    csilk_json_add_number(resp, "page", (double)page);
    csilk_json_add_number(resp, "page_size", (double)page_size);
    csilk_json_add_object(resp, "stats", stats_obj);

    *out_data = resp;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_admin_create_user(void*                          pool,
                               const admin_create_user_cmd_t* cmd,
                               csilk_ctx_t*                   c,
                               csilk_json_t**                 out_data,
                               auth_usecase_result_t*         out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || !cmd->username || !out_data) {
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
        OPENSSL_cleanse(password_buf, sizeof(password_buf));
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "%s", err);
        return -1;
    }

    const char* role = (cmd->role && cmd->role[0]) ? cmd->role : "user";
    if (mf_auth_rule_validate_role(role, err, sizeof(err)) != 0) {
        OPENSSL_cleanse(password_buf, sizeof(password_buf));
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "%s", err);
        return -1;
    }

    mf_user_t existing = {0};
    if (mf_auth_repo_find_by_username(pool, cmd->username, &existing) == 0) {
        OPENSSL_cleanse(password_buf, sizeof(password_buf));
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "用户名已存在");
        return -1;
    }

    char hashed[CSILK_BCRYPT_HASH_LEN];
    store_bcrypt_hash(password, hashed);
    OPENSSL_cleanse(password_buf, sizeof(password_buf));

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    db_tx_scope_t    scope;
    if (db_tx_scope_begin(db_pool, "mf_admin_create_user", &scope) != 0) {
        OPENSSL_cleanse(hashed, sizeof(hashed));
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    int64_t user_id = 0;
    if (mf_auth_repo_create_with_role(db_pool, cmd->username, hashed, role, &user_id) != 0 ||
        user_id <= 0) {
        OPENSSL_cleanse(hashed, sizeof(hashed));
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "创建用户失败");
        return -1;
    }
    OPENSSL_cleanse(hashed, sizeof(hashed));

    seed_new_user_defaults(db_pool, user_id);

    if (db_tx_scope_commit(db_pool, &scope) != 0) {
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "提交事务失败");
        return -1;
    }

    mf_user_t created = {0};
    if (mf_auth_repo_get_by_id(db_pool, user_id, &created) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "读取新建用户失败");
        return -1;
    }

    *out_data = user_to_summary_json(&created);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_admin_update_user(void*                          pool,
                               const admin_update_user_cmd_t* cmd,
                               csilk_json_t**                 out_data,
                               auth_usecase_result_t*         out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || cmd->target_user_id <= 0 || cmd->actor_user_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    db_tx_scope_t scope;
    if (db_tx_scope_begin((csilk_db_pool_t*)pool, "mf_admin_upd_user", &scope) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库事务开启失败");
        return -1;
    }

    mf_user_t target = {0};
    if (mf_auth_repo_get_by_id(pool, cmd->target_user_id, &target) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "用户不存在");
        return -1;
    }

    const char* current_role = target.role[0] ? target.role : "user";
    const char* next_role = (cmd->role && cmd->role[0]) ? cmd->role : current_role;
    bool        next_disabled = cmd->has_disabled ? cmd->disabled : target.disabled;
    bool        target_is_active_admin = (strcmp(current_role, "admin") == 0 && !target.disabled);
    int         active_admin_count = mf_auth_repo_count_active_admins(pool);
    if (active_admin_count < 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询管理员数量失败");
        return -1;
    }

    char err[256];
    if (mf_auth_rule_can_update_user(cmd->actor_user_id,
                                     cmd->target_user_id,
                                     target_is_active_admin,
                                     next_role,
                                     next_disabled,
                                     active_admin_count,
                                     err,
                                     sizeof(err)) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "%s", err);
        return -1;
    }

    bool changed_role = (cmd->role && cmd->role[0] && strcmp(cmd->role, current_role) != 0);
    bool changed_disabled = (cmd->has_disabled && cmd->disabled != target.disabled);

    if (changed_role && mf_auth_repo_update_role(pool, cmd->target_user_id, next_role) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "更新用户角色失败");
        return -1;
    }
    if (changed_disabled &&
        mf_auth_repo_set_disabled(pool, cmd->target_user_id, next_disabled) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "更新用户状态失败");
        return -1;
    }

    if ((changed_disabled && next_disabled) || changed_role) {
        if (mf_auth_repo_update_token_version(pool, cmd->target_user_id) != 0) {
            db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
            out_res->code = 500;
            snprintf(out_res->message, sizeof(out_res->message), "吊销用户会话失败");
            return -1;
        }
    }

    if (db_tx_scope_commit((csilk_db_pool_t*)pool, &scope) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "提交用户更新事务失败");
        return -1;
    }

    if ((changed_disabled && next_disabled) || changed_role) {
        invalidate_user_jwt(cmd->target_user_id);
    }

    if (out_data) {
        mf_user_t updated = {0};
        if (mf_auth_repo_get_by_id(pool, cmd->target_user_id, &updated) == 0) {
            *out_data = user_to_summary_json(&updated);
        }
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
auth_usecase_admin_reset_password(void*                             pool,
                                  const admin_reset_password_cmd_t* cmd,
                                  csilk_ctx_t*                      c,
                                  auth_usecase_result_t*            out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || cmd->target_user_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    mf_user_t target = {0};
    if (mf_auth_repo_get_by_id(pool, cmd->target_user_id, &target) != 0) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "用户不存在");
        return -1;
    }

    int         rc = -1;
    char        password_buf[512] = {0};
    char        new_hashed[CSILK_BCRYPT_HASH_LEN] = {0};
    const char* password = NULL;
    size_t      password_len = 0;
    if (decrypt_password_if_needed(c,
                                   cmd->new_password_enc,
                                   cmd->plain_password,
                                   password_buf,
                                   sizeof(password_buf),
                                   &password,
                                   &password_len) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "密码解密失败");
        goto cleanup;
    }

    char err[256];
    if (mf_auth_rule_validate_password(password, err, sizeof(err)) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "%s", err);
        goto cleanup;
    }

    store_bcrypt_hash(password, new_hashed);

    db_tx_scope_t scope;
    if (db_tx_scope_begin((csilk_db_pool_t*)pool, "mf_admin_reset_pw", &scope) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库事务开启失败");
        goto cleanup;
    }
    if (mf_auth_repo_update_password(pool, cmd->target_user_id, new_hashed) != 0 ||
        mf_auth_repo_update_token_version(pool, cmd->target_user_id) != 0 ||
        db_tx_scope_commit((csilk_db_pool_t*)pool, &scope) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "重置用户密码失败");
        goto cleanup;
    }

    invalidate_user_jwt(cmd->target_user_id);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "密码重置成功");
    rc = 0;

cleanup:
    OPENSSL_cleanse(password_buf, sizeof(password_buf));
    OPENSSL_cleanse(new_hashed, sizeof(new_hashed));
    return rc;
}

int
auth_usecase_admin_disable_2fa(void* pool, int64_t target_user_id, auth_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || target_user_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    mf_user_t target = {0};
    if (mf_auth_repo_get_by_id(pool, target_user_id, &target) != 0) {
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "用户不存在");
        return -1;
    }

    db_tx_scope_t scope;
    if (db_tx_scope_begin((csilk_db_pool_t*)pool, "mf_admin_dis_2fa", &scope) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库事务开启失败");
        return -1;
    }
    if (mf_auth_repo_disable_totp(pool, target_user_id) != 0 ||
        mf_auth_repo_update_token_version(pool, target_user_id) != 0 ||
        db_tx_scope_commit((csilk_db_pool_t*)pool, &scope) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "关闭用户两步验证失败");
        return -1;
    }

    invalidate_user_jwt(target_user_id);
    totp_replay_clear_user(target_user_id);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "已关闭该用户的两步验证");
    return 0;
}

int
auth_usecase_admin_delete_user(void*                  pool,
                               int64_t                actor_user_id,
                               int64_t                target_user_id,
                               auth_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || actor_user_id <= 0 || target_user_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    db_tx_scope_t scope;
    if (db_tx_scope_begin((csilk_db_pool_t*)pool, "mf_admin_del_user", &scope) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "数据库事务开启失败");
        return -1;
    }

    mf_user_t target = {0};
    if (mf_auth_repo_get_by_id(pool, target_user_id, &target) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "用户不存在");
        return -1;
    }

    const char* current_role = target.role[0] ? target.role : "user";
    bool        target_is_active_admin = (strcmp(current_role, "admin") == 0 && !target.disabled);
    int         active_admin_count = mf_auth_repo_count_active_admins(pool);
    if (active_admin_count < 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "查询管理员数量失败");
        return -1;
    }

    char err[256];
    if (mf_auth_rule_can_delete_user(actor_user_id,
                                     target_user_id,
                                     target_is_active_admin,
                                     active_admin_count,
                                     err,
                                     sizeof(err)) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "%s", err);
        return -1;
    }

    if (mf_auth_repo_delete_user_cascade(pool, target_user_id) != 0 ||
        db_tx_scope_commit((csilk_db_pool_t*)pool, &scope) != 0) {
        db_tx_scope_rollback((csilk_db_pool_t*)pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "删除用户失败");
        return -1;
    }

    invalidate_user_jwt(target_user_id);
    totp_replay_clear_user(target_user_id);
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "用户已删除");
    return 0;
}
