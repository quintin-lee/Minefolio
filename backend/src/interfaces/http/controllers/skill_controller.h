#pragma once

#include "csilk/csilk.h"
#include "csilk/app/app.h"

#ifdef __cplusplus
extern "C" {
#endif

void api_ai_skills_list(csilk_ctx_t* c);
void api_ai_skills_get(csilk_ctx_t* c);
void api_ai_skills_create(csilk_ctx_t* c);
void api_ai_skills_update(csilk_ctx_t* c);
void api_ai_skills_delete(csilk_ctx_t* c);
void api_ai_skills_toggle(csilk_ctx_t* c);
void api_ai_skills_test(csilk_ctx_t* c);
void api_ai_skills_import(csilk_ctx_t* c);
void api_ai_skills_export(csilk_ctx_t* c);
void api_ai_skills_reset_defaults(csilk_ctx_t* c);

void register_skill_routes(csilk_app_t* app);

#ifdef __cplusplus
}
#endif
