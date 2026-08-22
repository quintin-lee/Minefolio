#pragma once
#include "csilk/csilk.h"
#include "csilk/drivers/db.h"
void categories_seed_defaults(csilk_db_pool_t* pool, int64_t user_id);
void categories_list(csilk_ctx_t* c);
void categories_create(csilk_ctx_t* c);
void categories_update(csilk_ctx_t* c);
void categories_delete(csilk_ctx_t* c);
void categories_children(csilk_ctx_t* c);
#include "csilk/app/app.h"
void register_category_routes(csilk_app_t* app);
