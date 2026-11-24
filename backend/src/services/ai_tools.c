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
                               const volatile bool* cancel_token,
                               const csilk_json_t*  metadata)
{
    /* Parallel-tool worker threads pass pool=NULL (design §5: each thread
     * uses its own db_get_pool() for its own DB operations). Guard against
     * NULL here so the context is created with a safe pool pointer. */
    csilk_db_pool_t*   safe_pool = pool;
    ai_tool_context_t* ctx = ai_tool_context_create(safe_pool, user_id, session_id, NULL);
    if (ctx) {
        ctx->cancel_token = cancel_token;
        if (metadata) {
            if (ctx->metadata) {
                csilk_json_free(ctx->metadata);
            }
            ctx->metadata = csilk_json_copy(metadata);
        }
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
