#include "controllers/dca_controller.h"
#include "services/dca_service.h"

void
register_dca_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/dca/plans",
                      dca_service_list_plans,
                      NULL,
                      NULL,
                      "List DCA plans",
                      "Get user's DCA plans");
    csilk_app_post_ext(app,
                       "/api/dca/plans",
                       dca_service_create_plan,
                       NULL,
                       NULL,
                       "Create DCA plan",
                       "Create a new DCA plan");
    csilk_app_get_ext(app,
                      "/api/dca/plans/:id",
                      dca_service_get_plan,
                      NULL,
                      NULL,
                      "Get DCA plan",
                      "Get DCA plan details");
    csilk_app_put_ext(app,
                      "/api/dca/plans/:id",
                      dca_service_update_plan,
                      NULL,
                      NULL,
                      "Update DCA plan",
                      "Update DCA plan configuration");
    csilk_app_put_ext(app,
                      "/api/dca/plans/:id/status",
                      dca_service_set_plan_status,
                      NULL,
                      NULL,
                      "Set DCA plan status",
                      "Pause, resume, or complete DCA plan");
    csilk_app_delete_ext(app,
                         "/api/dca/plans/:id",
                         dca_service_delete_plan,
                         NULL,
                         NULL,
                         "Delete DCA plan",
                         "Delete DCA plan");

    csilk_app_get_ext(app,
                      "/api/dca/plans/:id/executions",
                      dca_service_list_executions,
                      NULL,
                      NULL,
                      "List DCA executions",
                      "Get execution history for plan");
    csilk_app_get_ext(app,
                      "/api/dca/executions/pending",
                      dca_service_list_pending_executions,
                      NULL,
                      NULL,
                      "List pending executions",
                      "Get pending DCA tasks for user");
    csilk_app_post_ext(app,
                       "/api/dca/executions/:id/confirm",
                       dca_service_confirm_execution,
                       NULL,
                       NULL,
                       "Confirm DCA execution",
                       "Confirm and execute DCA buy transaction");
    csilk_app_post_ext(app,
                       "/api/dca/executions/:id/skip",
                       dca_service_skip_execution,
                       NULL,
                       NULL,
                       "Skip DCA execution",
                       "Skip DCA execution task");
}
