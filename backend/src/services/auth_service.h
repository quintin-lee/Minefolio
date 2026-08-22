#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"
void auth_register(csilk_ctx_t* c);
void auth_login(csilk_ctx_t* c);
void auth_change_password(csilk_ctx_t* c);
void auth_me(csilk_ctx_t* c);
void register_auth_routes(csilk_app_t* app);
