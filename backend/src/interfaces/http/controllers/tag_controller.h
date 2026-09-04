#pragma once

#include "csilk/csilk.h"
#include "csilk/app/app.h"

void api_tags_list(csilk_ctx_t* c);
void api_tags_create(csilk_ctx_t* c);
void api_tags_update(csilk_ctx_t* c);
void api_tags_delete(csilk_ctx_t* c);
void api_tags_suggestions(csilk_ctx_t* c);

// Backward-compatibility aliases
void tags_list(csilk_ctx_t* c);
void tags_create(csilk_ctx_t* c);
void tags_update(csilk_ctx_t* c);
void tags_delete(csilk_ctx_t* c);
void tags_suggestions(csilk_ctx_t* c);

void register_tag_routes(csilk_app_t* app);
