#pragma once
#include "csilk/csilk.h"
void transactions_list(csilk_ctx_t* c);
void transactions_monthly(csilk_ctx_t* c);
void transactions_create(csilk_ctx_t* c);
void transactions_update(csilk_ctx_t* c);
void transactions_delete(csilk_ctx_t* c);

#include "csilk/app/app.h"
void register_transaction_routes(csilk_app_t* app);
#include "csilk/app/app.h"
void register_transaction_routes(csilk_app_t* app);
