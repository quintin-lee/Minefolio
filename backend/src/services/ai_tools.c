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
                        int64_t          session_id,
                        csilk_json_t*    args,
                        const char*      name)
{
    ai_tool_context_t* ctx = ai_tool_context_create(pool, user_id, session_id, NULL);
    char*              result = ai_tool_dispatch_parsed(ctx, name, args);
    ai_tool_context_free(ctx);
    return result;
}

/* Token-aware variant: 将运行时的协作式取消标志接入工具上下文，
   供 MCP bridge 在 spawn 前检查 cancel，避免用户取消后仍发起远端调用。 */
char*
ai_tools_execute_parsed_cancel(csilk_db_pool_t*     pool,
                               int64_t              user_id,
                               int64_t              session_id,
                               csilk_json_t*        args,
                               const char*          name,
                               const volatile bool* cancel_token)
{
    ai_tool_context_t* ctx = ai_tool_context_create(pool, user_id, session_id, NULL);
    if (ctx) {
        ctx->cancel_token = cancel_token;
    }
    char* result = ctx ? ai_tool_dispatch_parsed(ctx, name, args) : NULL;
    ai_tool_context_free(ctx);
    return result;
}

char*
ai_tools_execute(csilk_db_pool_t* pool,
                 int64_t          user_id,
                 int64_t          session_id,
                 const char*      name,
                 const char*      arguments)
{
    ai_tool_context_t* ctx = ai_tool_context_create(pool, user_id, session_id, NULL);
    char*              result = ai_tool_dispatch(ctx, name, arguments ? arguments : "{}");
    ai_tool_context_free(ctx);
    return result;
}
