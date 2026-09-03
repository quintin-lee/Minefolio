#include "services/ai_tools.h"
#include "services/ai/tools/registry.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/context.h"
#include "services/ai/tools/report_tool.h"
#include <stdlib.h>

const csilk_ai_tool_t*
ai_tools_get_definitions(size_t* count)
{
    return ai_tool_get_csilk_definitions(count);
}

char*
ai_tools_execute_parsed(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        csilk_json_t*    args,
                        const char*      name)
{
    ai_tool_context_t* ctx = ai_tool_context_create(pool, user_id, 0, NULL);
    char*              result = ai_tool_dispatch_parsed(ctx, name, args);
    ai_tool_context_free(ctx);
    return result;
}

char*
ai_tools_execute(csilk_db_pool_t* pool, int64_t user_id, const char* name, const char* arguments)
{
    ai_tool_context_t* ctx = ai_tool_context_create(pool, user_id, 0, NULL);
    char*              result = ai_tool_dispatch(ctx, name, arguments ? arguments : "{}");
    ai_tool_context_free(ctx);
    return result;
}
