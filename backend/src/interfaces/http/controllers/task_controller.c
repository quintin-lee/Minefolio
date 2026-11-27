#include "interfaces/http/controllers/task_controller.h"
#include "application/task/async_task_manager.h"
#include "common/ctx.h"
#include "common/db.h"
#include "common/response.h"

int
task_controller_init(csilk_db_pool_t* pool, int worker_threads)
{
    return mf_async_task_manager_init(pool, worker_threads);
}

void
task_controller_shutdown(void)
{
    mf_async_task_manager_shutdown();
}

static void
api_tasks_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* status = csilk_get_query(c, "status");
    const char* task_type = csilk_get_query(c, "task_type");

    int64_t page = 1;
    int64_t page_size = 20;
    parse_page_params(c, &page, &page_size);

    const char* ps_str = csilk_get_query(c, "page_size");
    const char* lim_str = csilk_get_query(c, "limit");
    if ((!ps_str || !ps_str[0]) && lim_str && lim_str[0]) {
        int64_t limit = parse_query_int_strict(lim_str, -1);
        if (limit <= 0) {
            respond_bad_request(c, "无效的 limit 参数");
            return;
        }
        page_size = (limit > 500) ? 500 : limit;
    }

    int64_t       total = 0;
    csilk_json_t* list = mf_async_task_list_paged(
        db_get_pool(), user_id, status, task_type, page, page_size, &total);
    respond_page_ok(c, list ? list : csilk_json_array(), total, page, page_size);
}

static void
api_tasks_get(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     task_id = parse_query_int_strict(id_str, -1);
    if (task_id <= 0) {
        respond_bad_request(c, "缺少或无效的 task_id");
        return;
    }

    csilk_json_t* task = mf_async_task_get(db_get_pool(), user_id, task_id);
    if (!task) {
        respond_not_found(c);
        return;
    }
    respond_ok(c, task);
}

void
register_task_routes(csilk_app_t* app)
{
    if (!app) {
        return;
    }
    csilk_app_get_ext(app,
                      "/api/tasks",
                      api_tasks_list,
                      nullptr,
                      nullptr,
                      "List async tasks",
                      "Returns recent asynchronous background tasks for the current user");
    csilk_app_get_ext(app,
                      "/api/tasks/:id",
                      api_tasks_get,
                      nullptr,
                      nullptr,
                      "Get async task status",
                      "Returns status, progress, result, or error details for a background task");
}
