#include "services/cashflow_service.h"
#include "interfaces/http/controllers/cashflow_controller.h"

void
cashflow_service_list_schedules(csilk_ctx_t* c)
{
    api_cashflow_list_schedules(c);
}

void
cashflow_service_create_schedule(csilk_ctx_t* c)
{
    api_cashflow_create_schedule(c);
}

void
cashflow_service_get_schedule(csilk_ctx_t* c)
{
    api_cashflow_get_schedule(c);
}

void
cashflow_service_update_schedule(csilk_ctx_t* c)
{
    api_cashflow_update_schedule(c);
}

void
cashflow_service_delete_schedule(csilk_ctx_t* c)
{
    api_cashflow_delete_schedule(c);
}

void
cashflow_service_get_calendar(csilk_ctx_t* c)
{
    api_cashflow_get_calendar(c);
}

void
cashflow_service_confirm(csilk_ctx_t* c)
{
    api_cashflow_confirm(c);
}
