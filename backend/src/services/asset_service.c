#include "services/asset_service.h"
#include "interfaces/http/controllers/asset_controller.h"

void
assets_list(csilk_ctx_t* c)
{
    api_assets_list(c);
}

void
assets_create(csilk_ctx_t* c)
{
    api_assets_create(c);
}

void
assets_update(csilk_ctx_t* c)
{
    api_assets_update(c);
}

void
assets_delete(csilk_ctx_t* c)
{
    api_assets_delete(c);
}

void
assets_detail(csilk_ctx_t* c)
{
    api_assets_detail(c);
}

void
assets_rebuild_single(csilk_ctx_t* c)
{
    api_assets_rebuild_single(c);
}

void
assets_rebuild_all(csilk_ctx_t* c)
{
    api_assets_rebuild_all(c);
}

void
register_asset_routes(csilk_app_t* app)
{
    register_asset_http_routes(app);
}
