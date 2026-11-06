#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/db.h"
#include "application/auth/commands.h"
#include "application/auth/dtos.h"
#include "application/auth/usecases.h"
#include "domain/auth/repository.h"

int main(void) {
    printf("=== Running test_oauth_security ===\n");
    unlink("/tmp/test_oauth_security.db");
    setenv("MINEFOLIO_DB_DSN", "/tmp/test_oauth_security.db", 1);
    setenv("DB_DRIVER", "sqlite", 1);
    setenv("JWT_SECRET", "test_secret_for_oauth_unit_tests_123456", 1);

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    /* 1. 缺少授权码 code：必须被 1002 拒绝 */
    oauth_callback_cmd_t spoof_cmd = {
        .provider = "github",
        .code = NULL,
        .username = "target_user",
    };
    csilk_json_t* out_data = NULL;
    auth_usecase_result_t res = {0};
    int rc = auth_usecase_oauth_callback(pool, &spoof_cmd, &out_data, &res);
    assert(rc != 0 && res.code == 1002);
    printf("PASS: test_oauth_callback_rejects_client_oauth_id_without_code\n");

    /* 2. 在未配置提供商 Client Secret 且非 Mock 模式下：必须报错 1002 */
    unsetenv("MINEFOLIO_TEST_OAUTH_MOCK");
    oauth_callback_cmd_t real_no_cfg = {
        .provider = "github",
        .code = "github_code_abc123",
    };
    memset(&res, 0, sizeof(res));
    rc = auth_usecase_oauth_callback(pool, &real_no_cfg, &out_data, &res);
    assert(rc != 0 && res.code == 1002);
    printf("PASS: test_oauth_callback_rejects_unconfigured_provider\n");

    /* 3. 启用测试 Mock 模式 */
    setenv("MINEFOLIO_TEST_OAUTH_MOCK", "1", 1);

    /* 3.1 失败的 Mock Code：必须报错 1002 */
    oauth_callback_cmd_t mock_fail_cmd = {
        .provider = "github",
        .code = "mock_invalid_code",
    };
    memset(&res, 0, sizeof(res));
    rc = auth_usecase_oauth_callback(pool, &mock_fail_cmd, &out_data, &res);
    assert(rc != 0 && res.code == 1002);
    printf("PASS: test_oauth_callback_mock_invalid_code_fails\n");

    /* 3.2 合法的 Mock Code：必须成功并返回登录令牌 */
    oauth_callback_cmd_t mock_ok_cmd = {
        .provider = "github",
        .code = "mock_code_user99",
        .username = "gh_user99",
    };
    memset(&res, 0, sizeof(res));
    rc = auth_usecase_oauth_callback(pool, &mock_ok_cmd, &out_data, &res);
    assert(rc == 0 && res.code == 0 && out_data != NULL);

    const char* token = csilk_json_get_string(out_data, "token");
    assert(token != NULL && token[0] != '\0');
    csilk_json_free(out_data);
    out_data = NULL;
    printf("PASS: test_oauth_callback_mock_valid_code_succeeds\n");

    /* 3.3 再次以相同 Mock Code 登录：应正常匹配现有账户 */
    memset(&res, 0, sizeof(res));
    rc = auth_usecase_oauth_callback(pool, &mock_ok_cmd, &out_data, &res);
    assert(rc == 0 && res.code == 0 && out_data != NULL);
    csilk_json_free(out_data);
    printf("PASS: test_oauth_callback_mock_subsequent_login_succeeds\n");

    unlink("/tmp/test_oauth_security.db");
    printf("=== All test_oauth_security tests passed ===\n");
    return 0;
}
