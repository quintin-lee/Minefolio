#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

void import_rule_service_list(csilk_ctx_t* c);
void import_rule_service_get(csilk_ctx_t* c);
void import_rule_service_create(csilk_ctx_t* c);
void import_rule_service_update(csilk_ctx_t* c);
void import_rule_service_delete(csilk_ctx_t* c);
void import_rule_service_reset_defaults(csilk_ctx_t* c);
