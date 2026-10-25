#pragma once

#include "csilk/csilk.h"

void api_accounts_list(csilk_ctx_t* c);
void api_accounts_create(csilk_ctx_t* c);
void api_accounts_update(csilk_ctx_t* c);
void api_accounts_delete(csilk_ctx_t* c);
void register_account_http_routes(csilk_app_t* app);
void register_account_routes(csilk_app_t* app);
