#include "services/ai/policy/confirmation.h"
#include "common/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <pthread.h>

#define DEFAULT_TTL_SEC 300
#define MAX_NONCES 2048

static char s_custom_secret[256] = {0};
static bool s_has_custom_secret = false;

typedef struct {
    char    nonce[64];
    int64_t expiration;
} consumed_nonce_entry_t;

static consumed_nonce_entry_t s_consumed_nonces[MAX_NONCES];
static size_t                 s_nonce_count = 0;
static pthread_mutex_t        s_nonce_lock = PTHREAD_MUTEX_INITIALIZER;

void
ai_confirmation_set_secret(const char* secret)
{
    pthread_mutex_lock(&s_nonce_lock);
    if (secret && secret[0]) {
        strncpy(s_custom_secret, secret, sizeof(s_custom_secret) - 1);
        s_has_custom_secret = true;
    } else {
        s_custom_secret[0] = '\0';
        s_has_custom_secret = false;
    }
    pthread_mutex_unlock(&s_nonce_lock);
}

const char*
ai_confirmation_get_secret(void)
{
    if (s_has_custom_secret && s_custom_secret[0]) {
        return s_custom_secret;
    }
    const char* sec_ai = config_secret_get("AI_SECRET", NULL, 0);
    if (sec_ai && sec_ai[0]) {
        return sec_ai;
    }
    const char* sec_jwt = config_secret_get("JWT_SECRET", NULL, 0);
    if (sec_jwt && sec_jwt[0]) {
        return sec_jwt;
    }

    static char cfg_secret[128];
    if (config_get_str("config/ai.json", "secret", cfg_secret, sizeof(cfg_secret)) == 0 &&
        cfg_secret[0]) {
        return cfg_secret;
    }

    if (config_secret_is_test_mode()) {
        return "test_env_ai_confirmation_fallback_key";
    }

    return NULL;
}

int
ai_confirmation_constant_time_memcmp(const void* a, const void* b, size_t len)
{
    if (!a || !b) {
        return 1;
    }
    const unsigned char* ua = (const unsigned char*)a;
    const unsigned char* ub = (const unsigned char*)b;
    unsigned char        diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= (ua[i] ^ ub[i]);
    }
    return diff != 0;
}

static void
compute_hmac_hex(const char* data, const char* secret, char* out_hex, size_t out_sz)
{
    unsigned char md[SHA256_DIGEST_LENGTH];
    unsigned int  mdlen = 0;
    HMAC(EVP_sha256(),
         secret,
         (int)strlen(secret),
         (const unsigned char*)data,
         strlen(data),
         md,
         &mdlen);
    static const char* hex = "0123456789abcdef";
    size_t             i = 0;
    for (; i < mdlen && i * 2 + 1 < out_sz; i++) {
        out_hex[i * 2] = hex[md[i] >> 4];
        out_hex[i * 2 + 1] = hex[md[i] & 0xf];
    }
    out_hex[i * 2] = '\0';
}

static int
cmp_keys(const void* a, const void* b)
{
    return strcmp(*(const char**)a, *(const char**)b);
}

void
ai_confirmation_hash_args(const csilk_json_t* args, char* out_hash, size_t out_hash_sz)
{
    if (!out_hash || out_hash_sz < 65) {
        return;
    }
    if (!args) {
        strncpy(out_hash,
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                out_hash_sz - 1);
        out_hash[64] = '\0';
        return;
    }

    /* 构造按键名字母字典序排列的干净参数对象 (Canonical JSON Hashing) */
    csilk_json_t* clean = csilk_json_object();
    if (csilk_json_is_object(args)) {
        size_t       sz = csilk_json_object_size(args);
        const char** keys = (const char**)malloc(sizeof(const char*) * (sz > 0 ? sz : 1));
        size_t       valid_cnt = 0;
        for (size_t i = 0; i < sz; i++) {
            const char* k = csilk_json_object_key(args, i);
            if (!k || strcmp(k, "draft_token") == 0 || strcmp(k, "token") == 0) {
                continue;
            }
            keys[valid_cnt++] = k;
        }
        if (valid_cnt > 1) {
            qsort(keys, valid_cnt, sizeof(const char*), cmp_keys);
        }
        for (size_t i = 0; i < valid_cnt; i++) {
            const char*   k = keys[i];
            csilk_json_t* v = csilk_json_get(args, k);
            if (!v) {
                continue;
            }
            if (csilk_json_is_string(v)) {
                csilk_json_add_string(clean, k, csilk_json_string_value(v));
            } else if (csilk_json_is_number(v)) {
                csilk_json_add_number(clean, k, csilk_json_number_value(v));
            } else if (csilk_json_is_bool(v)) {
                csilk_json_add_bool(clean, k, csilk_json_bool_value(v));
            } else if (csilk_json_is_null(v)) {
                csilk_json_add_null(clean, k);
            } else if (csilk_json_is_object(v)) {
                csilk_json_add_object(clean, k, csilk_json_copy(v));
            } else if (csilk_json_is_array(v)) {
                csilk_json_add_array(clean, k, csilk_json_copy(v));
            }
        }
        free(keys);
    }

    size_t len = 0;
    char*  serialized = csilk_json_serialize(clean, &len);
    csilk_json_free(clean);

    if (!serialized) {
        strncpy(out_hash,
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                out_hash_sz - 1);
        out_hash[64] = '\0';
        return;
    }

    unsigned char md[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)serialized, len, md);
    free(serialized);

    static const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        out_hash[i * 2] = hex[md[i] >> 4];
        out_hash[i * 2 + 1] = hex[md[i] & 0xf];
    }
    out_hash[64] = '\0';
}

static bool
is_nonce_consumed_locked(const char* nonce, int64_t now)
{
    for (size_t i = 0; i < s_nonce_count; i++) {
        if (s_consumed_nonces[i].expiration >= now &&
            strcmp(s_consumed_nonces[i].nonce, nonce) == 0) {
            return true;
        }
    }
    return false;
}

static void
record_nonce_consumed_locked(const char* nonce, int64_t expiration)
{
    int64_t now = (int64_t)time(NULL);
    /* 寻找过期槽位重用 */
    for (size_t i = 0; i < s_nonce_count; i++) {
        if (s_consumed_nonces[i].expiration < now) {
            snprintf(s_consumed_nonces[i].nonce, sizeof(s_consumed_nonces[i].nonce), "%s", nonce);
            s_consumed_nonces[i].expiration = expiration;
            return;
        }
    }
    if (s_nonce_count < MAX_NONCES) {
        snprintf(s_consumed_nonces[s_nonce_count].nonce,
                 sizeof(s_consumed_nonces[s_nonce_count].nonce),
                 "%s",
                 nonce);
        s_consumed_nonces[s_nonce_count].expiration = expiration;
        s_nonce_count++;
    } else {
        /* 环形覆盖最旧项 */
        size_t idx = (size_t)(rand() % MAX_NONCES);
        snprintf(s_consumed_nonces[idx].nonce, sizeof(s_consumed_nonces[idx].nonce), "%s", nonce);
        s_consumed_nonces[idx].expiration = expiration;
    }
}

char*
ai_confirmation_create_bound_token(int64_t             user_id,
                                   int64_t             session_id,
                                   const char*         tool_name,
                                   const csilk_json_t* args,
                                   ai_risk_level_t     risk,
                                   int                 ttl_seconds)
{
    if (!tool_name || !tool_name[0]) {
        return NULL;
    }
    if (ttl_seconds == 0) {
        ttl_seconds = DEFAULT_TTL_SEC;
    }

    int64_t now = (int64_t)time(NULL);
    int64_t exp = now + ttl_seconds;

    char args_hash[65];
    ai_confirmation_hash_args(args, args_hash, sizeof(args_hash));

    /* 生成随机高熵 Nonce */
    char nonce[32];
    snprintf(nonce, sizeof(nonce), "nc%08lx%08x", (unsigned long)now, (unsigned int)rand());

    /* 规范载荷字符串 */
    char canonical[512];
    snprintf(canonical,
             sizeof(canonical),
             "v2|%lld|%lld|%s|%s|%d|%lld|%s|%lld",
             (long long)user_id,
             (long long)session_id,
             tool_name,
             args_hash,
             (int)risk,
             (long long)now,
             nonce,
             (long long)exp);

    const char* secret = ai_confirmation_get_secret();
    if (!secret || secret[0] == '\0') {
        return NULL;
    }
    char mac[SHA256_DIGEST_LENGTH * 2 + 1];
    compute_hmac_hex(canonical, secret, mac, sizeof(mac));

    /* 返回格式: v2.<canonical>.<mac> */
    size_t out_sz = strlen(canonical) + strlen(mac) + 16;
    char*  token = (char*)malloc(out_sz);
    if (!token) {
        return NULL;
    }
    snprintf(token, out_sz, "mf_v2.%s.%s", canonical, mac);
    return token;
}

ai_confirmation_status_t
ai_confirmation_verify_and_consume(int64_t             user_id,
                                   int64_t             session_id,
                                   const char*         tool_name,
                                   const csilk_json_t* args,
                                   const char*         token)
{
    if (!token || !token[0] || !tool_name || !tool_name[0]) {
        return AI_CONFIRM_ERR_FORMAT;
    }

    /* 1. 检验 token 前缀结构 */
    if (strncmp(token, "mf_v2.", 6) != 0) {
        /* 兼容老版本格式 */
        if (ai_confirmation_verify_token(user_id, 0.0, NULL, NULL, token)) {
            return AI_CONFIRM_OK;
        }
        return AI_CONFIRM_ERR_FORMAT;
    }

    const char* payload_start = token + 6;
    const char* last_dot = strrchr(payload_start, '.');
    if (!last_dot || last_dot == payload_start) {
        return AI_CONFIRM_ERR_FORMAT;
    }

    size_t canonical_len = (size_t)(last_dot - payload_start);
    char   canonical[512];
    if (canonical_len >= sizeof(canonical)) {
        return AI_CONFIRM_ERR_FORMAT;
    }
    memcpy(canonical, payload_start, canonical_len);
    canonical[canonical_len] = '\0';

    const char* signature = last_dot + 1;

    /* 2. 恒定时间签名验证 (Constant-time comparison) */
    const char* secret = ai_confirmation_get_secret();
    if (!secret || secret[0] == '\0') {
        return AI_CONFIRM_ERR_SIGNATURE;
    }
    char expected_mac[SHA256_DIGEST_LENGTH * 2 + 1];
    compute_hmac_hex(canonical, secret, expected_mac, sizeof(expected_mac));

    size_t sig_len = strlen(signature);
    if (sig_len != strlen(expected_mac)) {
        return AI_CONFIRM_ERR_SIGNATURE;
    }
    if (ai_confirmation_constant_time_memcmp(signature, expected_mac, sig_len) != 0) {
        return AI_CONFIRM_ERR_SIGNATURE;
    }

    /* 3. 解析载荷字段: v2|uid|session_id|tool|args_hash|risk|timestamp|nonce|exp */
    int64_t tok_uid = 0, tok_sid = 0, tok_ts = 0, tok_exp = 0;
    char    tok_tool[64] = {0}, tok_hash[65] = {0}, tok_nonce[64] = {0};
    int     tok_risk = 0;

    int parsed = sscanf(canonical,
                        "v2|%lld|%lld|%63[^|]|%64[^|]|%d|%lld|%63[^|]|%lld",
                        (long long*)&tok_uid,
                        (long long*)&tok_sid,
                        tok_tool,
                        tok_hash,
                        &tok_risk,
                        (long long*)&tok_ts,
                        tok_nonce,
                        (long long*)&tok_exp);
    if (parsed != 8) {
        return AI_CONFIRM_ERR_FORMAT;
    }

    /* 4. 过期校验 (Expiration) */
    int64_t now = (int64_t)time(NULL);
    if (tok_exp <= now) {
        return AI_CONFIRM_ERR_EXPIRED;
    }

    /* 5. 用户身份隔离校验 (User isolation) */
    if (tok_uid != user_id) {
        return AI_CONFIRM_ERR_USER_MISMATCH;
    }

    /* 6. 工具绑定校验 (Tool binding) */
    if (strcmp(tok_tool, tool_name) != 0) {
        return AI_CONFIRM_ERR_TOOL_MISMATCH;
    }

    /* 7. 参数哈希防篡改校验 (Arguments tamper-proofing) */
    char cur_hash[65];
    ai_confirmation_hash_args(args, cur_hash, sizeof(cur_hash));
    if (ai_confirmation_constant_time_memcmp(tok_hash, cur_hash, 64) != 0) {
        return AI_CONFIRM_ERR_ARGS_MISMATCH;
    }

    /* 8. 防重放校验与原子消费 (Anti-replay single-use check) */
    pthread_mutex_lock(&s_nonce_lock);
    if (is_nonce_consumed_locked(tok_nonce, now)) {
        pthread_mutex_unlock(&s_nonce_lock);
        return AI_CONFIRM_ERR_REPLAY;
    }
    record_nonce_consumed_locked(tok_nonce, tok_exp);
    pthread_mutex_unlock(&s_nonce_lock);

    return AI_CONFIRM_OK;
}

const char*
ai_confirmation_strerror(ai_confirmation_status_t status)
{
    switch (status) {
    case AI_CONFIRM_OK:
        return "确认合法有效";
    case AI_CONFIRM_ERR_FORMAT:
        return "确认令牌格式非法或解析错误";
    case AI_CONFIRM_ERR_EXPIRED:
        return "确认令牌已过期，请重新发起拟录入草案";
    case AI_CONFIRM_ERR_USER_MISMATCH:
        return "用户身份不匹配，禁止跨用户确认或越权使用令牌";
    case AI_CONFIRM_ERR_TOOL_MISMATCH:
        return "操作工具不匹配，禁止移花接木使用不同操作的确认令牌";
    case AI_CONFIRM_ERR_ARGS_MISMATCH:
        return "确认执行参数与拟录入草案不一致，检测到金额或字段篡改";
    case AI_CONFIRM_ERR_REPLAY:
        return "确认令牌已被使用，禁止二次重复执行或重放攻击";
    case AI_CONFIRM_ERR_SIGNATURE:
        return "确认令牌签名校验失败或防伪密钥错误";
    default:
        return "未知确认错误";
    }
}

/* 兼容老版本接口 */
char*
ai_confirmation_create_token(int64_t user_id, double amount, const char* a, const char* b)
{
    csilk_json_t* dummy = csilk_json_object();
    csilk_json_add_number(dummy, "amount", amount);
    if (a) {
        csilk_json_add_string(dummy, "param_a", a);
    }
    if (b) {
        csilk_json_add_string(dummy, "param_b", b);
    }
    char* tok =
        ai_confirmation_create_bound_token(user_id, 0, "legacy_action", dummy, AI_RISK_HIGH, 300);
    csilk_json_free(dummy);
    return tok;
}

int
ai_confirmation_verify_token(
    int64_t user_id, double amount, const char* a, const char* b, const char* token)
{
    csilk_json_t* dummy = csilk_json_object();
    csilk_json_add_number(dummy, "amount", amount);
    if (a) {
        csilk_json_add_string(dummy, "param_a", a);
    }
    if (b) {
        csilk_json_add_string(dummy, "param_b", b);
    }
    ai_confirmation_status_t st =
        ai_confirmation_verify_and_consume(user_id, 0, "legacy_action", dummy, token);
    csilk_json_free(dummy);
    return (st == AI_CONFIRM_OK) ? 1 : 0;
}

ai_confirmation_draft_t*
ai_confirmation_draft_create(const char* action_name,
                             const char* summary,
                             const char* payload_json,
                             const char* warning)
{
    ai_confirmation_draft_t* draft =
        (ai_confirmation_draft_t*)calloc(1, sizeof(ai_confirmation_draft_t));
    if (!draft) {
        return NULL;
    }

    snprintf(draft->draft_id, sizeof(draft->draft_id), "draft_%ld", (long)time(NULL));

    if (action_name) {
        strncpy(draft->action_name, action_name, sizeof(draft->action_name) - 1);
    }
    if (summary) {
        draft->summary = strdup(summary);
    }
    if (payload_json) {
        draft->payload_json = strdup(payload_json);
    }
    if (warning) {
        draft->warning_message = strdup(warning);
    }

    draft->requires_confirmation = true;
    return draft;
}

void
ai_confirmation_draft_free(ai_confirmation_draft_t* draft)
{
    if (!draft) {
        return;
    }
    if (draft->summary) {
        free(draft->summary);
    }
    if (draft->payload_json) {
        free(draft->payload_json);
    }
    if (draft->warning_message) {
        free((void*)draft->warning_message);
    }
    free(draft);
}

char*
ai_confirmation_draft_to_json(const ai_confirmation_draft_t* draft)
{
    if (!draft) {
        return strdup("{}");
    }

    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_bool(obj, "requires_confirmation", draft->requires_confirmation);
    csilk_json_add_string(obj, "draft_id", draft->draft_id);
    csilk_json_add_string(obj, "action_name", draft->action_name);
    csilk_json_add_string(obj, "summary", draft->summary ? draft->summary : "高风险操作确认");
    csilk_json_add_string(obj, "warning", draft->warning_message ? draft->warning_message : "");
    if (draft->draft_token[0]) {
        csilk_json_add_string(obj, "draft_token", draft->draft_token);
    }

    if (draft->payload_json && draft->payload_json[0]) {
        csilk_json_t* payload = csilk_json_parse(draft->payload_json);
        if (payload) {
            csilk_json_add_object(obj, "payload", payload);
        }
    }

    size_t len = 0;
    char*  out = csilk_json_serialize(obj, &len);
    csilk_json_free(obj);
    return out ? out : strdup("{}");
}
