#include "services/ai/runtime/runtime.h"
#include "services/ai/tools/registry.h"
#include "services/ai_service.h"

void
ai_runtime_init(csilk_db_pool_t* pool)
{
    ai_tool_registry_init();
    ai_init(pool);
}

void
ai_runtime_shutdown(void)
{
    ai_shutdown();
}

ai_config_t*
ai_runtime_get_config(void)
{
    return ai_get_config();
}
