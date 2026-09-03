#include "services/market_service.h"
#include "interfaces/http/controllers/market_controller.h"
#include "application/market/usecases.h"

int
market_service_do_sync_user(csilk_db_pool_t* pool,
                            int64_t          user_id,
                            int*             out_synced,
                            int*             out_failed)
{
    return market_usecase_do_sync_user(pool, user_id, out_synced, out_failed);
}

void
market_service_search(csilk_ctx_t* c)
{
    api_market_search(c);
}

void
market_service_quote(csilk_ctx_t* c)
{
    api_market_quote(c);
}

void
market_service_sync_all(csilk_ctx_t* c)
{
    api_market_sync_all(c);
}

void
market_service_sync_single(csilk_ctx_t* c)
{
    api_market_sync_single(c);
}

void
market_service_price_history(csilk_ctx_t* c)
{
    api_market_price_history(c);
}

void
market_service_get_settings(csilk_ctx_t* c)
{
    api_market_get_settings(c);
}

void
market_service_update_settings(csilk_ctx_t* c)
{
    api_market_update_settings(c);
}

void
market_service_test_proxy(csilk_ctx_t* c)
{
    api_market_test_proxy(c);
}

void
market_service_get_exchange_rates(csilk_ctx_t* c)
{
    api_market_get_exchange_rates(c);
}

void
market_service_update_exchange_rate(csilk_ctx_t* c)
{
    api_market_update_exchange_rate(c);
}

void
market_service_get_fx_history(csilk_ctx_t* c)
{
    api_market_get_fx_history(c);
}
