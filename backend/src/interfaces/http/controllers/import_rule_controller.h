#pragma once

#include "csilk/csilk.h"
#include "csilk/app/app.h"

void api_import_rule_list(csilk_ctx_t* c);
void api_import_rule_get(csilk_ctx_t* c);
void api_import_rule_create(csilk_ctx_t* c);
void api_import_rule_update(csilk_ctx_t* c);
void api_import_rule_delete(csilk_ctx_t* c);
void api_import_rule_reset_defaults(csilk_ctx_t* c);

// Backward-compatibility aliases
void import_rule_service_list(csilk_ctx_t* c);
void import_rule_service_get(csilk_ctx_t* c);
void import_rule_service_create(csilk_ctx_t* c);
void import_rule_service_update(csilk_ctx_t* c);
void import_rule_service_delete(csilk_ctx_t* c);
void import_rule_service_reset_defaults(csilk_ctx_t* c);

void register_import_rule_routes(csilk_app_t* app);
