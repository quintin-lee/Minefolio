#include "controllers/cashflow_controller.h"
#include "services/cashflow_service.h"

void
register_cashflow_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/cashflow/schedules",
                      cashflow_service_list_schedules,
                      NULL,
                      NULL,
                      "List cashflow schedules",
                      "Get user's cashflow schedules");
    csilk_app_post_ext(app,
                       "/api/cashflow/schedules",
                       cashflow_service_create_schedule,
                       NULL,
                       NULL,
                       "Create cashflow schedule",
                       "Create a new cashflow schedule");
    csilk_app_get_ext(app,
                      "/api/cashflow/schedules/:id",
                      cashflow_service_get_schedule,
                      NULL,
                      NULL,
                      "Get cashflow schedule",
                      "Get cashflow schedule details");
    csilk_app_put_ext(app,
                      "/api/cashflow/schedules/:id",
                      cashflow_service_update_schedule,
                      NULL,
                      NULL,
                      "Update cashflow schedule",
                      "Update cashflow schedule configuration");
    csilk_app_delete_ext(app,
                         "/api/cashflow/schedules/:id",
                         cashflow_service_delete_schedule,
                         NULL,
                         NULL,
                         "Delete cashflow schedule",
                         "Delete cashflow schedule");

    csilk_app_get_ext(app,
                      "/api/cashflow/calendar",
                      cashflow_service_get_calendar,
                      NULL,
                      NULL,
                      "Get cashflow calendar",
                      "Get monthly cashflow events and summary");
    csilk_app_post_ext(app,
                       "/api/cashflow/confirm",
                       cashflow_service_confirm,
                       NULL,
                       NULL,
                       "Confirm cashflow income",
                       "Confirm passive income and create transaction");
}
