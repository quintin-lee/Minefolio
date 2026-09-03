#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "config/secret.h"

static int
mock_vault_manager(const char* key, char* out, size_t out_size)
{
    if (strcmp(key, "VAULT_SECRET_KEY") == 0) {
        strncpy(out, "vault_production_secure_token_9999", out_size - 1);
        out[out_size - 1] = '\0';
        return 0;
    }
    return -1;
}

int
main(void)
{
    printf("====================================================\n");
    printf("  Minefolio Unified Secret Provider Test Suite      \n");
    printf("====================================================\n");

    /* ------------------------------------------------------------- */
    /* Test 1: Environment Provider                                  */
    /* ------------------------------------------------------------- */
    printf("[Test 1] Environment Provider (prefix & alias resolution)...\n");
    {
        setenv("MINEFOLIO_TEST_ENV_SEC", "real_secure_crypto_entropy_value_123", 1);
        char buf[256];

        /* 通过纯键名 "TEST_ENV_SEC" 读取 */
        const char* val1 = config_secret_get("TEST_ENV_SEC", buf, sizeof(buf));
        assert(val1 != NULL);
        assert(strcmp(val1, "real_secure_crypto_entropy_value_123") == 0);

        /* 通过全名 "MINEFOLIO_TEST_ENV_SEC" 读取 */
        const char* val2 = config_secret_get("MINEFOLIO_TEST_ENV_SEC", buf, sizeof(buf));
        assert(val2 != NULL);
        assert(strcmp(val2, "real_secure_crypto_entropy_value_123") == 0);

        /* TLS 缓冲调用验证 (out 为 NULL) */
        const char* val_tls = config_secret_get("TEST_ENV_SEC", NULL, 0);
        assert(val_tls != NULL);
        assert(strcmp(val_tls, "real_secure_crypto_entropy_value_123") == 0);

        printf("  ✅ Environment provider resolved with prefix & TLS buffer\n");
    }

    /* ------------------------------------------------------------- */
    /* Test 2: File Provider (Docker Secret / Path File)              */
    /* ------------------------------------------------------------- */
    printf("[Test 2] File Provider (Docker Secret file path)...\n");
    {
        char temp_path[] = "/tmp/minefolio_secret_test_XXXXXX";
        int fd = mkstemp(temp_path);
        assert(fd >= 0);
        const char* secret_content = "super_secure_vault_file_secret_2026\n\r  \n";
        write(fd, secret_content, strlen(secret_content));
        close(fd);

        /* 设置 MINEFOLIO_FILE_TEST_SEC_FILE 环境变量指向临时凭证文件 */
        setenv("MINEFOLIO_FILE_TEST_SEC_FILE", temp_path, 1);

        char buf[256];
        const char* val = config_secret_get("FILE_TEST_SEC", buf, sizeof(buf));
        assert(val != NULL);
        /* 验证尾部换行与空白已被自动去除 */
        assert(strcmp(val, "super_secure_vault_file_secret_2026") == 0);

        unlink(temp_path);
        printf("  ✅ File provider securely loaded and trimmed file secret\n");
    }

    /* ------------------------------------------------------------- */
    /* Test 3: Secret Manager Provider (Vault / AWS / K8s Plugin)    */
    /* ------------------------------------------------------------- */
    printf("[Test 3] External Secret Manager Provider Extension...\n");
    {
        config_secret_set_manager(mock_vault_manager);

        char buf[256];
        const char* val = config_secret_get("VAULT_SECRET_KEY", buf, sizeof(buf));
        assert(val != NULL);
        assert(strcmp(val, "vault_production_secure_token_9999") == 0);

        /* 未收录的 key 正常回退 */
        const char* missing = config_secret_get("NON_EXISTENT_KEY_XYZ", buf, sizeof(buf));
        assert(missing == NULL);

        config_secret_set_manager(NULL);
        printf("  ✅ External Secret Manager callback registered and resolved successfully\n");
    }

    /* ------------------------------------------------------------- */
    /* Test 4: Test Mode Isolation & Overrides                       */
    /* ------------------------------------------------------------- */
    printf("[Test 4] Test Isolation & In-Memory Overrides...\n");
    {
        config_secret_clear_test_overrides();

        /* 设置测试覆盖 */
        config_secret_set_test_override("JWT_SECRET", "mock_test_jwt_secret_token");

        char buf[256];
        const char* val = config_secret_get("JWT_SECRET", buf, sizeof(buf));
        assert(val != NULL);
        assert(strcmp(val, "mock_test_jwt_secret_token") == 0);

        /* 清除测试覆盖 */
        config_secret_clear_test_overrides();
        printf("  ✅ Test mode override isolation verified\n");
    }

    /* ------------------------------------------------------------- */
    /* Test 5: Production Safety Gate (Forbidden Weak Secret Rejection)*/
    /* ------------------------------------------------------------- */
    printf("[Test 5] Production Safety Gate (Forbidden Placeholder Rejection)...\n");
    {
        /* 切换为严格生产模式 (Test Mode = false) */
        config_secret_set_test_mode(false);
        assert(config_secret_is_test_mode() == false);

        /* 注入禁止的弱口令环境 */
        setenv("MINEFOLIO_BAD_SECRET_1", "change-me-in-production", 1);
        setenv("MINEFOLIO_BAD_SECRET_2", "my-default-secret-key", 1);
        setenv("MINEFOLIO_BAD_SECRET_3", "hard-coded-secret-xyz", 1);

        char buf[256];
        /* 生产模式下必须全部拒绝并返回 NULL */
        const char* r1 = config_secret_get("BAD_SECRET_1", buf, sizeof(buf));
        assert(r1 == NULL);

        const char* r2 = config_secret_get("BAD_SECRET_2", buf, sizeof(buf));
        assert(r2 == NULL);

        const char* r3 = config_secret_get("BAD_SECRET_3", buf, sizeof(buf));
        assert(r3 == NULL);

        /* 校验有效安全密钥在生产模式下不受影响 */
        setenv("MINEFOLIO_VALID_PROD_SEC", "a8fbc9103e8293bd827103847291aebc918237", 1);
        const char* r_ok = config_secret_get("VALID_PROD_SEC", buf, sizeof(buf));
        assert(r_ok != NULL);
        assert(strcmp(r_ok, "a8fbc9103e8293bd827103847291aebc918237") == 0);

        /* 恢复测试模式 */
        config_secret_set_test_mode(true);
        printf("  ✅ Production security gate rejected all weak placeholders\n");
    }

    /* ------------------------------------------------------------- */
    /* Test 6: config_env_get & Default Fallback                     */
    /* ------------------------------------------------------------- */
    printf("[Test 6] config_env_get with fallback defaults...\n");
    {
        const char* def1 = config_env_get("NON_EXISTING_ENV_FLAG", NULL, 0, "fallback_default");
        assert(def1 != NULL);
        assert(strcmp(def1, "fallback_default") == 0);

        setenv("MINEFOLIO_CUSTOM_PORT", "9090", 1);
        const char* p = config_env_get("CUSTOM_PORT", NULL, 0, "8080");
        assert(p != NULL);
        assert(strcmp(p, "9090") == 0);
        printf("  ✅ config_env_get handles environment and fallback defaults\n");
    }

    printf("\n🎉 ALL UNIFIED SECRET PROVIDER TESTS PASSED!\n");
    return 0;
}
