#include "services/ai_workflow_service.h"
#include "services/ai/workflow/engine.h"
#include "services/ai/workflow/executor.h"
#include "infrastructure/repositories/ai_session_repo_impl.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/response.h"

csilk_json_t*
ai_workflow_get_definitions_json(void)
{
    return ai_workflow_get_all_definitions_json();
}

void
ai_workflow_run_handler(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "请求体必须为 JSON");
        return;
    }

    const char*         workflow_id = csilk_json_get_string(body, "workflow_id");
    int64_t             session_id = (int64_t)db_get_num(body, "session_id");
    const csilk_json_t* params = csilk_json_get(body, "params");

    if (!workflow_id || !workflow_id[0]) {
        csilk_json_free(body);
        respond_bad_request(c, "缺少 workflow_id");
        return;
    }

    const ai_workflow_graph_t* graph = ai_workflow_find(workflow_id);
    if (!graph) {
        csilk_json_free(body);
        respond_bad_request(c, "未找到指定的工作流");
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    if (session_id > 0) {
        csilk_json_t* sess = mf_ai_session_get(pool, user_id, session_id);
        if (!sess) {
            csilk_json_free(body);
            respond_not_found(c);
            return;
        }
        csilk_json_free(sess);
    }

    ai_wf_context_t* wf_ctx = ai_wf_context_create(pool, user_id, session_id, params);
    /* 用户提示：前端传递展示文本（默认为工作流标题），用于落库到会话历史 */
    const char* user_content = csilk_json_get_string(body, "content");
    snprintf(wf_ctx->user_prompt,
             sizeof(wf_ctx->user_prompt),
             "%s",
             (user_content && user_content[0]) ? user_content : "🪄 启动工作流");
    ai_workflow_execute_stream(c, graph, wf_ctx);

    ai_wf_context_free(wf_ctx);
    csilk_json_free(body);
}
