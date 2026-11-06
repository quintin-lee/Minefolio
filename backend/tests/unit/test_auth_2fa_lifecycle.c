#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/db.h"
#include "common/jwt.h"
#include "config/secret.h"
#include "application/auth/commands.h"
#include "application/auth/dtos.h"
#include "application/auth/usecases.h"
#include "domain/auth/repository.h"

int main(void) {
    printf("=== Running test_auth_2fa_lifecycle ===\n");
    unlink("/tmp/test_auth_2fa_lifecycle.db");
    setenv("MINEFOLIO_DB_DSN", "/tmp/test_auth_2fa_lifecycle.db", 1);
    setenv("DB_DRIVER", "sqlite", 1);
    setenv("JWT_SECRET", "test_secret_for_2fa_unit_tests_123456", 1);

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    /* 1. 测试 jwt_generate_temp_token 生成的载荷规范 */
    char* temp_token = jwt_generate_temp_token(NULL, 9999, 5, 300);
    assert(temp_token != NULL);

    csilk_jwt_options_t opts = {
        .algorithm = CSILK_JWT_HS256,
        .flags = CSILK_JWT_REQUIRE_EXP,
        .leeway_sec = 0,
    };
    const char* secret = "test_secret_for_2fa_unit_tests_123456";
    csilk_json_t* payload = csilk_jwt_verify_options(NULL, temp_token, secret, strlen(secret), &opts);
    assert(payload != NULL);

    int64_t sub = (int64_t)csilk_json_get_number(payload, "sub");
    int v = (int)csilk_json_get_number(payload, "v");
    const char* type = csilk_json_get_string(payload, "type");
    int64_t iat = (int64_t)csilk_json_get_number(payload, "iat");
    int64_t exp = (int64_t)csilk_json_get_number(payload, "exp");

    assert(sub == 9999);
    assert(v == 5);
    assert(type != NULL && strcmp(type, "2fa_pending") == 0);
    assert(exp - iat == 300);
    csilk_json_free(payload);
    printf("PASS: test_temp_token_claims\n");

    /* 2. 创建测试用户并开启 TOTP */
    register_user_cmd_t reg_cmd = {
        .username = "user_2fa_test",
        .plain_password = "Password123!",
    };
    csilk_json_t* reg_data = NULL;
    auth_usecase_result_t reg_res = {0};
    assert(auth_usecase_register(pool, &reg_cmd, NULL, &reg_data, &reg_res) == 0);
    csilk_json_free(reg_data);

    mf_user_t test_user = {0};
    assert(mf_auth_repo_find_by_username(pool, "user_2fa_test", &test_user) == 0);
    int64_t test_uid = test_user.id;
    assert(test_uid > 0);

    /* 手动设置 TOTP secret 并启用 */
    mf_auth_repo_set_totp_secret(pool, test_uid, "JBSWY3DPEHPK3PXP");
    mf_auth_repo_enable_totp(pool, test_uid, "[\"BACKUP01\"]");

    /* 3. 测试 auth_usecase_2fa_verify_login 拒绝非 2fa_pending 的普通 token */
    char* normal_token = jwt_generate_token(NULL, test_uid, 0);
    assert(normal_token != NULL);

    verify_2fa_login_cmd_t verify_cmd_bad = {
        .temp_token = normal_token,
        .code = "123456",
    };
    csilk_json_t* verify_data = NULL;
    auth_usecase_result_t verify_res = {0};
    int rc = auth_usecase_2fa_verify_login(pool, &verify_cmd_bad, &verify_data, &verify_res);
    assert(rc != 0 || verify_res.code != 0);
    printf("PASS: test_2fa_verify_login_rejects_normal_token\n");
    free(normal_token);

    /* 4. 测试 auth_usecase_2fa_disable: 无密码无code必须被拒绝 (1002) */
    disable_2fa_cmd_t dis_cmd_empty = {
        .user_id = test_uid,
        .password = NULL,
        .code = NULL,
    };
    auth_usecase_result_t dis_res = {0};
    rc = auth_usecase_2fa_disable(pool, &dis_cmd_empty, NULL, &dis_res);
    assert(rc != 0 && dis_res.code == 1002);
    printf("PASS: test_2fa_disable_rejects_empty_credentials\n");

    /* 5. 测试 auth_usecase_2fa_disable: 错误密码必须被拒绝 (1002) */
    disable_2fa_cmd_t dis_cmd_wrong_pw = {
        .user_id = test_uid,
        .password = "WrongPassword!",
        .code = NULL,
    };
    memset(&dis_res, 0, sizeof(dis_res));
    rc = auth_usecase_2fa_disable(pool, &dis_cmd_wrong_pw, NULL, &dis_res);
    assert(rc != 0 && dis_res.code == 1002);
    printf("PASS: test_2fa_disable_rejects_wrong_password\n");

    /* 6. 测试 auth_usecase_2fa_disable: 正确密码解绑成功 */
    disable_2fa_cmd_t dis_cmd_ok = {
        .user_id = test_uid,
        .password = "Password123!",
        .code = NULL,
    };
    memset(&dis_res, 0, sizeof(dis_res));
    rc = auth_usecase_2fa_disable(pool, &dis_cmd_ok, NULL, &dis_res);
    assert(rc == 0 && dis_res.code == 0);
    printf("PASS: test_2fa_disable_succeeds_with_correct_password\n");

    free(temp_token);
    unlink("/tmp/test_auth_2fa_lifecycle.db");
    printf("=== All test_auth_2fa_lifecycle tests passed ===\n");
    return 0;
}
