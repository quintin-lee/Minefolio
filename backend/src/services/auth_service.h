#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"
void auth_register(csilk_ctx_t* c);
void auth_login(csilk_ctx_t* c);
void auth_change_password(csilk_ctx_t* c);
void auth_me(csilk_ctx_t* c);
void auth_public_key(csilk_ctx_t* c);
void auth_2fa_status(csilk_ctx_t* c);
void auth_2fa_setup(csilk_ctx_t* c);
void auth_2fa_enable(csilk_ctx_t* c);
void auth_2fa_disable(csilk_ctx_t* c);
void auth_2fa_verify_login(csilk_ctx_t* c);
void auth_oauth_providers(csilk_ctx_t* c);
void auth_oauth_callback(csilk_ctx_t* c);
void register_auth_routes(csilk_app_t* app);
