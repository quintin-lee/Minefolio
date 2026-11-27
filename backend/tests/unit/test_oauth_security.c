#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/core_names.h>
#include "csilk/core/primitives/codec.h"
#include "common/db.h"
#include "application/auth/commands.h"
#include "application/auth/dtos.h"
#include "application/auth/usecases.h"
#include "domain/auth/repository.h"

static void
make_signed_jwt(EVP_PKEY* pkey,
                const char* hdr_json,
                const char* pay_json,
                char* out_jwt,
                size_t out_cap)
{
    char hdr_b64[512] = {0};
    char pay_b64[1024] = {0};
    csilk_base64url_encode((const uint8_t*)hdr_json, strlen(hdr_json), hdr_b64);
    csilk_base64url_encode((const uint8_t*)pay_json, strlen(pay_json), pay_b64);

    char signing[1600];
    snprintf(signing, sizeof(signing), "%s.%s", hdr_b64, pay_b64);

    if (!pkey) {
        snprintf(out_jwt, out_cap, "%s.unsigned_sig", signing);
        return;
    }

    uint8_t sig[512];
    size_t  sig_len = sizeof(sig);
    EVP_MD_CTX* mctx = EVP_MD_CTX_new();
    assert(mctx != NULL);
    assert(EVP_DigestSignInit(mctx, NULL, EVP_sha256(), NULL, pkey) == 1);
    assert(EVP_DigestSign(mctx, sig, &sig_len, (const uint8_t*)signing, strlen(signing)) == 1);
    EVP_MD_CTX_free(mctx);

    char sig_b64[1024] = {0};
    csilk_base64url_encode(sig, sig_len, sig_b64);
    snprintf(out_jwt, out_cap, "%s.%s", signing, sig_b64);
}

static void
test_oidc_id_token_and_provider_gating(void)
{
    /* Generate test RSA keypair & JWKS */
    EVP_PKEY* pkey = EVP_RSA_gen(2048);
    assert(pkey != NULL);

    BIGNUM* bn_n = NULL;
    BIGNUM* bn_e = NULL;
    assert(EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_N, &bn_n) == 1);
    assert(EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_E, &bn_e) == 1);
    uint8_t n_bytes[512], e_bytes[64];
    int     n_len = BN_bn2bin(bn_n, n_bytes);
    int     e_len = BN_bn2bin(bn_e, e_bytes);
    char    n_b64[1024] = {0}, e_b64[128] = {0};
    csilk_base64url_encode(n_bytes, (size_t)n_len, n_b64);
    csilk_base64url_encode(e_bytes, (size_t)e_len, e_b64);
    BN_free(bn_n);
    BN_free(bn_e);

    char jwks[2048];
    snprintf(jwks,
             sizeof(jwks),
             "{\"keys\":[{\"kty\":\"RSA\",\"use\":\"sig\",\"alg\":\"RS256\",\"kid\":\"k1\","
             "\"n\":\"%s\",\"e\":\"%s\"}]}",
             n_b64,
             e_b64);

    char jwt[4096], sub[128], uname[128];
    const char* hdr_rs = "{\"alg\":\"RS256\",\"typ\":\"JWT\",\"kid\":\"k1\"}";

    /* Valid RS256 ID token with array aud + matching azp */
    make_signed_jwt(pkey,
                    hdr_rs,
                    "{\"iss\":\"https://idp.example\",\"sub\":\"oidc-u-1\",\"aud\":[\"mf-app\","
                    "\"other\"],\"azp\":\"mf-app\",\"exp\":2000000100,\"preferred_username\":"
                    "\"alice_oidc\"}",
                    jwt,
                    sizeof(jwt));
    assert(auth_oidc_verify_id_token(
               jwt, "mf-app", "https://idp.example", jwks, 2000000000, sub, sizeof(sub), uname, sizeof(uname)) ==
           0);
    assert(strcmp(sub, "oidc-u-1") == 0);
    assert(strcmp(uname, "alice_oidc") == 0);

    /* Expired token rejected */
    assert(auth_oidc_verify_id_token(
               jwt, "mf-app", "https://idp.example", jwks, 2000000200, sub, sizeof(sub), uname, sizeof(uname)) !=
           0);

    /* Wrong issuer rejected */
    assert(auth_oidc_verify_id_token(
               jwt, "mf-app", "https://evil.example", jwks, 2000000000, sub, sizeof(sub), uname, sizeof(uname)) !=
           0);

    /* Wrong aud rejected */
    assert(auth_oidc_verify_id_token(
               jwt, "wrong-client", "https://idp.example", jwks, 2000000000, sub, sizeof(sub), uname, sizeof(uname)) !=
           0);

    /* alg=none MUST always be rejected even when jwks_json is NULL */
    make_signed_jwt(NULL,
                    "{\"alg\":\"none\"}",
                    "{\"iss\":\"https://idp.example\",\"sub\":\"oidc-u-1\",\"aud\":\"mf-app\","
                    "\"exp\":2000000100}",
                    jwt,
                    sizeof(jwt));
    assert(auth_oidc_verify_id_token(
               jwt, "mf-app", "https://idp.example", NULL, 2000000000, sub, sizeof(sub), uname, sizeof(uname)) !=
           0);

    /* Tampered signature MUST fail JWKS verification */
    make_signed_jwt(pkey,
                    hdr_rs,
                    "{\"iss\":\"https://idp.example\",\"sub\":\"oidc-u-1\",\"aud\":\"mf-app\","
                    "\"exp\":2000000100}",
                    jwt,
                    sizeof(jwt));
    jwt[strlen(jwt) - 1] = (jwt[strlen(jwt) - 1] == 'A') ? 'B' : 'A';
    assert(auth_oidc_verify_id_token(
               jwt, "mf-app", "https://idp.example", jwks, 2000000000, sub, sizeof(sub), uname, sizeof(uname)) !=
           0);

    EVP_PKEY_free(pkey);

    /* OIDC provider listing requires CLIENT_ID + AUTH_URL + TOKEN_URL */
    setenv("OAUTH_OIDC_CLIENT_ID", "oidc_cid_test", 1);
    setenv("OAUTH_OIDC_AUTH_URL", "https://idp.example/auth", 1);
    unsetenv("OAUTH_OIDC_TOKEN_URL");
    unsetenv("MINEFOLIO_OAUTH_OIDC_TOKEN_URL");
    csilk_json_t*         pdata = NULL;
    auth_usecase_result_t pres = {0};
    assert(auth_usecase_oauth_providers(&pdata, &pres) == 0 && pdata != NULL);
    const csilk_json_t* parr = csilk_json_get(pdata, "providers");
    assert(parr && csilk_json_array_size(parr) == 0);
    csilk_json_free(pdata);

    setenv("OAUTH_OIDC_TOKEN_URL", "https://idp.example/token", 1);
    pdata = NULL;
    assert(auth_usecase_oauth_providers(&pdata, &pres) == 0 && pdata != NULL);
    parr = csilk_json_get(pdata, "providers");
    assert(parr && csilk_json_array_size(parr) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(parr, 0), "id"), "oidc") == 0);
    csilk_json_free(pdata);
    unsetenv("OAUTH_OIDC_CLIENT_ID");
    unsetenv("OAUTH_OIDC_AUTH_URL");
    unsetenv("OAUTH_OIDC_TOKEN_URL");

    printf("PASS: test_oidc_id_token_and_provider_gating\n");
}

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
    auth_oauth_mock_reset(); /* 启动期固化：mock 关闭 */
    oauth_callback_cmd_t real_no_cfg = {
        .provider = "github",
        .code = "github_code_abc123",
    };
    memset(&res, 0, sizeof(res));
    rc = auth_usecase_oauth_callback(pool, &real_no_cfg, &out_data, &res);
    assert(rc != 0 && res.code == 1002);
    printf("PASS: test_oauth_callback_rejects_unconfigured_provider\n");

    /* 3. 启用测试 Mock 模式（改变环境变量后必须重新固化，模拟新进程启动） */
    setenv("MINEFOLIO_TEST_OAUTH_MOCK", "1", 1);
    auth_oauth_mock_reset();

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

    /* 4. 注册开放门禁：ENABLE_REGISTRATION=0 且已有用户时拒绝后续注册 */
    setenv("ENABLE_REGISTRATION", "0", 1);
    register_user_cmd_t reg_closed = {
        .username = "late_comer",
        .plain_password = "Password123!",
    };
    csilk_json_t* reg_data = NULL;
    auth_usecase_result_t reg_res = {0};
    int reg_rc = auth_usecase_register(pool, &reg_closed, NULL, &reg_data, &reg_res);
    assert(reg_rc != 0 && reg_res.code == 1004);
    printf("PASS: test_register_rejected_when_disabled_with_existing_users\n");

    /* 4b. 注册开放门禁：ENABLE_REGISTRATION=0 时 OAuth 新用户注册同样必须被 1004 拒绝 */
    oauth_callback_cmd_t oauth_closed_cmd = {
        .provider = "github",
        .code = "mock_code_new_blocked",
        .username = "gh_blocked_user",
    };
    out_data = NULL;
    memset(&res, 0, sizeof(res));
    rc = auth_usecase_oauth_callback(pool, &oauth_closed_cmd, &out_data, &res);
    assert(rc != 0 && res.code == 1004 && out_data == NULL);
    printf("PASS: test_oauth_callback_new_user_rejected_when_registration_disabled\n");

    /* 5. 恢复开放注册（默认），验证新用户名可注册成功 */
    setenv("ENABLE_REGISTRATION", "1", 1);
    register_user_cmd_t reg_open = {
        .username = "late_comer2",
        .plain_password = "Password123!",
    };
    reg_data = NULL;
    memset(&reg_res, 0, sizeof(reg_res));
    reg_rc = auth_usecase_register(pool, &reg_open, NULL, &reg_data, &reg_res);
    assert(reg_rc == 0 && reg_res.code == 0);
    csilk_json_free(reg_data);
    printf("PASS: test_register_allowed_when_open_with_existing_users\n");

    /* 6. 已启用 2FA 的 OAuth 老用户回调必须返回 require_2fa=true + temp_token，禁止直接下发正式 JWT */
    mf_user_t gh_user = {0};
    assert(mf_auth_repo_find_by_username(pool, "gh_user99", &gh_user) == 0);
    assert(mf_auth_repo_set_totp_secret(pool, gh_user.id, "JBSWY3DPEHPK3PXP") == 0);
    assert(mf_auth_repo_enable_totp(pool, gh_user.id, "[\"OAUTHBACKUP1\"]") == 0);

    out_data = NULL;
    memset(&res, 0, sizeof(res));
    rc = auth_usecase_oauth_callback(pool, &mock_ok_cmd, &out_data, &res);
    assert(rc == 0 && res.code == 0 && out_data != NULL);
    assert(csilk_json_as_bool(csilk_json_get(out_data, "require_2fa"), false) == true);
    const char* oauth_temp_tok = csilk_json_get_string(out_data, "temp_token");
    assert(oauth_temp_tok != NULL && strlen(oauth_temp_tok) > 10);
    assert(csilk_json_get(out_data, "token") == NULL);

    verify_2fa_login_cmd_t oauth_2fa_cmd = {
        .temp_token = oauth_temp_tok,
        .code = "OAUTHBACKUP1",
    };
    csilk_json_t* oauth_2fa_data = NULL;
    auth_usecase_result_t oauth_2fa_res = {0};
    assert(auth_usecase_2fa_verify_login(pool, &oauth_2fa_cmd, &oauth_2fa_data, &oauth_2fa_res) == 0);
    assert(oauth_2fa_res.code == 0 && oauth_2fa_data != NULL);
    assert(csilk_json_get_string(oauth_2fa_data, "token") != NULL);
    csilk_json_free(oauth_2fa_data);
    csilk_json_free(out_data);
    out_data = NULL;
    printf("PASS: test_oauth_callback_requires_2fa_when_enabled\n");

    unlink("/tmp/test_oauth_security.db");
    test_oidc_id_token_and_provider_gating();
    printf("=== All test_oauth_security tests passed ===\n");
    return 0;
}
