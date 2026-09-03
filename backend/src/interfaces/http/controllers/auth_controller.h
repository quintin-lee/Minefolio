#pragma once

#include "csilk/csilk.h"

void api_auth_register(csilk_ctx_t* c);
void api_auth_login(csilk_ctx_t* c);
void api_auth_public_key(csilk_ctx_t* c);
void api_auth_me(csilk_ctx_t* c);
void api_auth_change_password(csilk_ctx_t* c);
void api_auth_2fa_status(csilk_ctx_t* c);
void api_auth_2fa_setup(csilk_ctx_t* c);
void api_auth_2fa_enable(csilk_ctx_t* c);
void api_auth_2fa_disable(csilk_ctx_t* c);
void api_auth_2fa_verify_login(csilk_ctx_t* c);
void api_auth_oauth_providers(csilk_ctx_t* c);
void api_auth_oauth_callback(csilk_ctx_t* c);

void register_auth_routes(csilk_app_t* app);
