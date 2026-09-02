#include "controllers/import_rule_controller.h"
#include "services/import_rule_service.h"

static void
handle_list(csilk_ctx_t* c)
{
    import_rule_service_list(c);
}

static void
handle_get(csilk_ctx_t* c)
{
    import_rule_service_get(c);
}

static void
handle_create(csilk_ctx_t* c)
{
    import_rule_service_create(c);
}

static void
handle_update(csilk_ctx_t* c)
{
    import_rule_service_update(c);
}

static void
handle_delete(csilk_ctx_t* c)
{
    import_rule_service_delete(c);
}

static void
handle_reset_defaults(csilk_ctx_t* c)
{
    import_rule_service_reset_defaults(c);
}

void
register_import_rule_routes(csilk_app_t* app)
{
    csilk_app_get(app, "/api/import-rules", handle_list);
    csilk_app_post(app, "/api/import-rules", handle_create);
    csilk_app_post(app, "/api/import-rules/reset-defaults", handle_reset_defaults);
    csilk_app_get(app, "/api/import-rules/:id", handle_get);
    csilk_app_put(app, "/api/import-rules/:id", handle_update);
    csilk_app_delete(app, "/api/import-rules/:id", handle_delete);
}
