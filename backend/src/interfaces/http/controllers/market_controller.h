#pragma once

#include "csilk/csilk.h"

void api_market_search(csilk_ctx_t* c);
void api_market_quote(csilk_ctx_t* c);
void api_market_sync_all(csilk_ctx_t* c);
void api_market_sync_single(csilk_ctx_t* c);
void api_market_price_history(csilk_ctx_t* c);
void api_market_get_settings(csilk_ctx_t* c);
void api_market_update_settings(csilk_ctx_t* c);
void api_market_test_proxy(csilk_ctx_t* c);
void api_market_get_exchange_rates(csilk_ctx_t* c);
void api_market_update_exchange_rate(csilk_ctx_t* c);
void api_market_get_fx_history(csilk_ctx_t* c);

void register_market_routes(csilk_app_t* app);
