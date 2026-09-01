#pragma once
#include "csilk/csilk.h"

void market_service_search(csilk_ctx_t* c);
void market_service_quote(csilk_ctx_t* c);
void market_service_sync_all(csilk_ctx_t* c);
void market_service_sync_single(csilk_ctx_t* c);
void market_service_price_history(csilk_ctx_t* c);
void market_service_get_settings(csilk_ctx_t* c);
void market_service_update_settings(csilk_ctx_t* c);
void market_service_test_proxy(csilk_ctx_t* c);
void market_service_get_exchange_rates(csilk_ctx_t* c);

/* Internal sync logic used by both HTTP controller and background scheduler */
int market_service_do_sync_user(csilk_db_pool_t* pool,
                                int64_t          user_id,
                                int*             out_synced,
                                int*             out_failed);
