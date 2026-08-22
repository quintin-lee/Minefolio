#pragma once
#include "csilk/csilk.h"
void assets_list(csilk_ctx_t* c);
void assets_create(csilk_ctx_t* c);
void assets_update(csilk_ctx_t* c);
void assets_delete(csilk_ctx_t* c);
void assets_detail(csilk_ctx_t* c);
#include "csilk/app/app.h"
void register_asset_routes(csilk_app_t* app);
