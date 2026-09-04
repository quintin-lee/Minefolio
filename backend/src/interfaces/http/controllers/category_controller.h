#pragma once

#include "csilk/csilk.h"
#include "csilk/app/app.h"

void api_categories_list(csilk_ctx_t* c);
void api_categories_create(csilk_ctx_t* c);
void api_categories_update(csilk_ctx_t* c);
void api_categories_delete(csilk_ctx_t* c);
void api_categories_children(csilk_ctx_t* c);

// Backward-compatibility aliases
void categories_list(csilk_ctx_t* c);
void categories_create(csilk_ctx_t* c);
void categories_update(csilk_ctx_t* c);
void categories_delete(csilk_ctx_t* c);
void categories_children(csilk_ctx_t* c);

void register_category_routes(csilk_app_t* app);
