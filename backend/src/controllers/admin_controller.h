#pragma once
#include "csilk/csilk.h"
void system_status(csilk_ctx_t* c);
void system_setup(csilk_ctx_t* c);
#include "csilk/app/app.h"
void register_admin_routes(csilk_app_t* app);
