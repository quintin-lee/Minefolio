#pragma once
#include "csilk/csilk.h"
#include "config/key_manager.h"
void system_status(csilk_ctx_t* c);
void system_setup(csilk_ctx_t* c);
void auth_register(csilk_ctx_t* c);
void auth_login(csilk_ctx_t* c);
void auth_change_password(csilk_ctx_t* c);
void auth_me(csilk_ctx_t* c);
#include "csilk/app/app.h"
void register_auth_routes(csilk_app_t* app);
