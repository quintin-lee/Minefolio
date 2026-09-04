#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "config/secret.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

#define MAX_TEST_OVERRIDES 32
#define TLS_BUF_SIZE 2048

typedef struct {
    char key[128];
    char value[1024];
} test_override_t;

static pthread_mutex_t   s_secret_lock = PTHREAD_MUTEX_INITIALIZER;
static secret_manager_fn s_secret_manager = NULL;
static test_override_t   s_test_overrides[MAX_TEST_OVERRIDES];
static size_t            s_override_count = 0;
static bool              s_test_mode = false;
static bool              s_test_mode_initialized = false;

static _Thread_local char   s_tls_secret_ring[4][TLS_BUF_SIZE];
static _Thread_local size_t s_tls_secret_idx = 0;
static _Thread_local char   s_tls_env_ring[4][TLS_BUF_SIZE];
static _Thread_local size_t s_tls_env_idx = 0;

static const char* const FORBIDDEN_PATTERNS[] = {
    "change-me", "default-secret", "hard-coded-secret", "replace-with-", NULL};

static void
init_test_mode_from_env(void)
{
    if (s_test_mode_initialized) {
        return;
    }
    const char* env_test = getenv("MINEFOLIO_TEST");
    const char* env_ci = getenv("CI");
    if ((env_test && env_test[0] && strcmp(env_test, "0") != 0) ||
        (env_ci && env_ci[0] && strcmp(env_ci, "0") != 0)) {
        s_test_mode = true;
    }
    s_test_mode_initialized = true;
}

void
config_secret_set_test_mode(bool enabled)
{
    pthread_mutex_lock(&s_secret_lock);
    s_test_mode = enabled;
    s_test_mode_initialized = true;
    pthread_mutex_unlock(&s_secret_lock);
}

bool
config_secret_is_test_mode(void)
{
    pthread_mutex_lock(&s_secret_lock);
    init_test_mode_from_env();
    bool mode = s_test_mode;
    pthread_mutex_unlock(&s_secret_lock);
    return mode;
}

void
config_secret_set_manager(secret_manager_fn fn)
{
    pthread_mutex_lock(&s_secret_lock);
    s_secret_manager = fn;
    pthread_mutex_unlock(&s_secret_lock);
}

void
config_secret_set_test_override(const char* key, const char* value)
{
    if (!key || !key[0]) {
        return;
    }
    pthread_mutex_lock(&s_secret_lock);
    s_test_mode = true;
    s_test_mode_initialized = true;

    for (size_t i = 0; i < s_override_count; i++) {
        if (strcasecmp(s_test_overrides[i].key, key) == 0) {
            if (value) {
                strncpy(s_test_overrides[i].value, value, sizeof(s_test_overrides[i].value) - 1);
                s_test_overrides[i].value[sizeof(s_test_overrides[i].value) - 1] = '\0';
            } else {
                /* Remove override */
                s_test_overrides[i] = s_test_overrides[s_override_count - 1];
                s_override_count--;
            }
            pthread_mutex_unlock(&s_secret_lock);
            return;
        }
    }

    if (value && s_override_count < MAX_TEST_OVERRIDES) {
        strncpy(s_test_overrides[s_override_count].key,
                key,
                sizeof(s_test_overrides[s_override_count].key) - 1);
        s_test_overrides[s_override_count].key[sizeof(s_test_overrides[s_override_count].key) - 1] =
            '\0';
        strncpy(s_test_overrides[s_override_count].value,
                value,
                sizeof(s_test_overrides[s_override_count].value) - 1);
        s_test_overrides[s_override_count]
            .value[sizeof(s_test_overrides[s_override_count].value) - 1] = '\0';
        s_override_count++;
    }
    pthread_mutex_unlock(&s_secret_lock);
}

void
config_secret_clear_test_overrides(void)
{
    pthread_mutex_lock(&s_secret_lock);
    s_override_count = 0;
    pthread_mutex_unlock(&s_secret_lock);
}

static bool
contains_forbidden_pattern(const char* val)
{
    if (!val) {
        return false;
    }
    for (int i = 0; FORBIDDEN_PATTERNS[i] != NULL; i++) {
        if (strcasestr(val, FORBIDDEN_PATTERNS[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static int
read_secret_from_file(const char* filepath, char* out, size_t out_size)
{
    if (!filepath || !filepath[0] || !out || out_size == 0) {
        return -1;
    }
    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        return -1;
    }

    size_t n = fread(out, 1, out_size - 1, fp);
    fclose(fp);
    out[n] = '\0';

    /* 去除尾部换行与空白 */
    while (n > 0 &&
           (out[n - 1] == '\r' || out[n - 1] == '\n' || out[n - 1] == ' ' || out[n - 1] == '\t')) {
        out[--n] = '\0';
    }

    return (n > 0) ? 0 : -1;
}

const char*
config_secret_get(const char* key, char* out, size_t out_size)
{
    if (!key || !key[0]) {
        return NULL;
    }

    char*  target_buf = out ? out : s_tls_secret_ring[(s_tls_secret_idx++) & 3];
    size_t target_sz = out ? out_size : sizeof(s_tls_secret_ring[0]);
    target_buf[0] = '\0';

    pthread_mutex_lock(&s_secret_lock);
    init_test_mode_from_env();
    bool is_test = s_test_mode;

    /* 1. 检查内存测试覆盖 (Test Overrides) */
    for (size_t i = 0; i < s_override_count; i++) {
        if (strcasecmp(s_test_overrides[i].key, key) == 0 ||
            (strncasecmp(key, "MINEFOLIO_", 10) == 0 &&
             strcasecmp(s_test_overrides[i].key, key + 10) == 0)) {
            strncpy(target_buf, s_test_overrides[i].value, target_sz - 1);
            target_buf[target_sz - 1] = '\0';
            pthread_mutex_unlock(&s_secret_lock);
            return target_buf;
        }
    }

    /* 2. 检查外部 Secret Manager 回调 */
    if (s_secret_manager) {
        if (s_secret_manager(key, target_buf, target_sz) == 0 && target_buf[0] != '\0') {
            pthread_mutex_unlock(&s_secret_lock);
            goto validate_and_return;
        }
    }
    pthread_mutex_unlock(&s_secret_lock);

    /* 3. 环境变量提供者 (Environment Provider) */
    /* 尝试 key 原名 */
    const char* env_val = getenv(key);
    /* 尝试带前缀 MINEFOLIO_<KEY> */
    if (!env_val && strncasecmp(key, "MINEFOLIO_", 10) != 0) {
        char full_env[128];
        snprintf(full_env, sizeof(full_env), "MINEFOLIO_%s", key);
        env_val = getenv(full_env);
    }
    /* 若传参本身带 MINEFOLIO_，尝试去掉前缀检索 */
    if (!env_val && strncasecmp(key, "MINEFOLIO_", 10) == 0) {
        env_val = getenv(key + 10);
    }

    if (env_val && env_val[0] != '\0') {
        strncpy(target_buf, env_val, target_sz - 1);
        target_buf[target_sz - 1] = '\0';
        goto validate_and_return;
    }

    /* 4. 文件系统 Secret Provider (File Provider) */
    /* 4a. 检查 <KEY>_FILE 或 MINEFOLIO_<KEY>_FILE 环境变量指定路径 */
    char file_env[128];
    if (strncasecmp(key, "MINEFOLIO_", 10) == 0) {
        snprintf(file_env, sizeof(file_env), "%s_FILE", key);
    } else {
        snprintf(file_env, sizeof(file_env), "MINEFOLIO_%s_FILE", key);
    }
    const char* custom_file = getenv(file_env);
    if (custom_file && custom_file[0] != '\0') {
        if (read_secret_from_file(custom_file, target_buf, target_sz) == 0) {
            goto validate_and_return;
        }
    }

    /* 4b. 检查容器标准挂载路径: /run/secrets/<key_lower> */
    const char* pure_name = (strncasecmp(key, "MINEFOLIO_", 10) == 0) ? (key + 10) : key;
    char        lower_name[64];
    size_t      klen = strlen(pure_name);
    if (klen < sizeof(lower_name)) {
        for (size_t i = 0; i < klen; i++) {
            lower_name[i] = (char)tolower((unsigned char)pure_name[i]);
        }
        lower_name[klen] = '\0';

        char secret_path[256];
        /* /run/secrets/xxx (Docker / Kubernetes secret 挂载) */
        snprintf(secret_path, sizeof(secret_path), "/run/secrets/%s", lower_name);
        if (read_secret_from_file(secret_path, target_buf, target_sz) == 0) {
            goto validate_and_return;
        }

        /* config/secrets/xxx (本地安全目录) */
        snprintf(secret_path, sizeof(secret_path), "config/secrets/%s", lower_name);
        if (read_secret_from_file(secret_path, target_buf, target_sz) == 0) {
            goto validate_and_return;
        }
    }

    return NULL;

validate_and_return:
    /* 5. 生产安全校验：非测试模式严禁使用默认占位弱口令 */
    if (!is_test && contains_forbidden_pattern(target_buf)) {
        fprintf(stderr,
                "[CRITICAL SECURITY ERROR] Secret '%s' matched forbidden default pattern. "
                "Production execution refused! Please configure a secure secret.\n",
                key);
        target_buf[0] = '\0';
        return NULL;
    }

    return target_buf;
}

const char*
config_env_get(const char* key, char* out, size_t out_size, const char* default_val)
{
    if (!key || !key[0]) {
        return default_val;
    }

    char*  target_buf = out ? out : s_tls_env_ring[(s_tls_env_idx++) & 3];
    size_t target_sz = out ? out_size : sizeof(s_tls_env_ring[0]);
    target_buf[0] = '\0';

    /* 先尝试读取 Secret Provider (包括可能配置在文件或外部 Manager 中的情况) */
    const char* sec = config_secret_get(key, target_buf, target_sz);
    if (sec && sec[0] != '\0') {
        return sec;
    }

    /* 普通环境变量检索 */
    const char* val = getenv(key);
    if (!val && strncasecmp(key, "MINEFOLIO_", 10) != 0) {
        char full_env[128];
        snprintf(full_env, sizeof(full_env), "MINEFOLIO_%s", key);
        val = getenv(full_env);
    }
    if (!val && strncasecmp(key, "MINEFOLIO_", 10) == 0) {
        val = getenv(key + 10);
    }

    if (val && val[0] != '\0') {
        strncpy(target_buf, val, target_sz - 1);
        target_buf[target_sz - 1] = '\0';
        return target_buf;
    }

    if (default_val) {
        strncpy(target_buf, default_val, target_sz - 1);
        target_buf[target_sz - 1] = '\0';
        return target_buf;
    }

    return NULL;
}

bool
config_secret_is_valid(const char* key)
{
    char        buf[128];
    const char* val = config_secret_get(key, buf, sizeof(buf));
    return (val != NULL && val[0] != '\0');
}
