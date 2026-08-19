#pragma once
#include "csilk/csilk.h"
void tags_list(csilk_ctx_t* c);
void tags_create(csilk_ctx_t* c);
void tags_update(csilk_ctx_t* c);
void tags_delete(csilk_ctx_t* c);
void tags_suggestions(csilk_ctx_t* c);
#include "csilk/app/app.h"
void register_tag_routes(csilk_app_t* app);
