#include "controllers/ai_trace_controller.h"
#include "services/ai_trace_service.h"

void
register_ai_trace_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/ai/traces",
                      ai_trace_service_list,
                      NULL,
                      NULL,
                      "List AI traces",
                      "Returns paginated AI conversation traces");
    csilk_app_get_ext(app,
                      "/api/ai/traces/stats",
                      ai_trace_service_stats,
                      NULL,
                      NULL,
                      "AI trace stats",
                      "Returns aggregate trace statistics");
    csilk_app_get_ext(app,
                      "/api/ai/traces/:id",
                      ai_trace_service_get,
                      NULL,
                      NULL,
                      "Get AI trace",
                      "Returns full trace detail including messages");
}
