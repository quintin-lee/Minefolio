#include "interfaces/http/controllers/auth_controller.h"
#include "application/auth/usecases.h"
#include "config/key_manager.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
api_auth_register(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    register_user_cmd_t cmd = {
        .username = csilk_json_get_string(body, "username"),
        .password_enc = csilk_json_get_string(body, "password_enc"),
        .plain_password = csilk_json_get_string(body, "password"),
    };

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_register(db_get_pool(), &cmd, c, &out_data, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
api_auth_login(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    login_user_cmd_t cmd = {
        .username = csilk_json_get_string(body, "username"),
        .password_enc = csilk_json_get_string(body, "password_enc"),
        .plain_password = csilk_json_get_string(body, "password"),
    };

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_login(db_get_pool(), &cmd, c, &out_data, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 1001, res.message);
    }
}

void
api_auth_public_key(csilk_ctx_t* c)
{
    auth_public_key(c);
}

void
api_auth_me(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_me(db_get_pool(), user_id, &out_data, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 1001, res.message);
    }
}

void
api_auth_change_password(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    change_password_cmd_t cmd = {
        .user_id = user_id,
        .old_password_enc = csilk_json_get_string(body, "old_password_enc"),
        .new_password_enc = csilk_json_get_string(body, "new_password_enc"),
    };

    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_change_password(db_get_pool(), &cmd, c, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* resp = csilk_json_object();
        csilk_json_add_number(resp, "code", 0);
        csilk_json_add_string(resp, "message", "密码修改成功");
        respond_ok(c, resp);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
api_auth_2fa_status(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        respond_unauthorized(c);
        return;
    }

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_2fa_status(db_get_pool(), user_id, &out_data, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 1001, res.message);
    }
}

void
api_auth_2fa_setup(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_2fa_setup(db_get_pool(), user_id, &out_data, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_auth_2fa_enable(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    enable_2fa_cmd_t cmd = {
        .user_id = user_id,
        .code = csilk_json_get_string(body, "code"),
    };

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_2fa_enable(db_get_pool(), &cmd, &out_data, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
api_auth_2fa_disable(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_2fa_disable(db_get_pool(), user_id, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else {
        respond_error(c, res.code ? res.code : 1001, res.message);
    }
}

void
api_auth_2fa_verify_login(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    verify_2fa_login_cmd_t cmd = {
        .temp_token = csilk_json_get_string(body, "temp_token"),
        .code = csilk_json_get_string(body, "code"),
    };

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_2fa_verify_login(db_get_pool(), &cmd, &out_data, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
api_auth_oauth_providers(csilk_ctx_t* c)
{
    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_oauth_providers(&out_data, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_auth_oauth_callback(csilk_ctx_t* c)
{
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "无效的 JSON 数据");
        return;
    }

    oauth_callback_cmd_t cmd = {
        .provider = csilk_json_get_string(body, "provider"),
        .code = csilk_json_get_string(body, "code"),
        .oauth_id = csilk_json_get_string(body, "oauth_id"),
        .username = csilk_json_get_string(body, "username"),
    };

    csilk_json_t*         out_data = NULL;
    auth_usecase_result_t res = {0};
    int                   rc = auth_usecase_oauth_callback(db_get_pool(), &cmd, &out_data, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, out_data);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
register_auth_routes(csilk_app_t* app)
{
    csilk_app_post_ext(app,
                       "/api/auth/register",
                       api_auth_register,
                       "register_req_t",
                       "token_resp_t",
                       "Register admin",
                       "Create the first admin user (only allowed before system initialization)");
    csilk_app_post_ext(app,
                       "/api/auth/login",
                       api_auth_login,
                       "login_req_t",
                       "token_resp_t",
                       "Login",
                       "Authenticate with username and RSA-encrypted password, returns JWT token");
    csilk_app_get_ext(app,
                      "/api/auth/public-key",
                      api_auth_public_key,
                      NULL,
                      NULL,
                      "Get public key",
                      "Returns the RSA public key PEM for client-side password encryption");
    csilk_app_get_ext(app,
                      "/api/auth/me",
                      api_auth_me,
                      NULL,
                      "user_resp_t",
                      "Get current user profile",
                      "Returns the authenticated user's profile");
    csilk_app_put_ext(app,
                      "/api/auth/password",
                      api_auth_change_password,
                      "change_pwd_req_t",
                      NULL,
                      "Change password",
                      "Update the current user's password using encrypted old/new values");
    csilk_app_get_ext(app,
                      "/api/auth/2fa/status",
                      api_auth_2fa_status,
                      NULL,
                      NULL,
                      "Get 2FA status",
                      "Check if TOTP 2FA is enabled for current user");
    csilk_app_post_ext(app,
                       "/api/auth/2fa/setup",
                       api_auth_2fa_setup,
                       NULL,
                       NULL,
                       "Setup 2FA",
                       "Generate a new TOTP secret and OTPAuth URL");
    csilk_app_post_ext(app,
                       "/api/auth/2fa/enable",
                       api_auth_2fa_enable,
                       NULL,
                       NULL,
                       "Enable 2FA",
                       "Verify code and enable TOTP 2FA, returns backup codes");
    csilk_app_post_ext(app,
                       "/api/auth/2fa/disable",
                       api_auth_2fa_disable,
                       NULL,
                       NULL,
                       "Disable 2FA",
                       "Disable TOTP 2FA for current user");
    csilk_app_post_ext(app,
                       "/api/auth/2fa/verify-login",
                       api_auth_2fa_verify_login,
                       NULL,
                       NULL,
                       "Verify 2FA login",
                       "Verify TOTP code or backup code with temp token to complete login");
    csilk_app_get_ext(app,
                      "/api/auth/oauth/providers",
                      api_auth_oauth_providers,
                      NULL,
                      NULL,
                      "Get OAuth providers",
                      "Returns list of active OAuth2 / OIDC providers");
    csilk_app_post_ext(app,
                       "/api/auth/oauth/callback",
                       api_auth_oauth_callback,
                       NULL,
                       NULL,
                       "OAuth callback",
                       "Exchange code for token and authenticate/provision user");
}
