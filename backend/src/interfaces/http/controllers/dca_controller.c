/**
 * @file dca_controller.c
 * @brief 定投计划 HTTP 控制器 (Interfaces Layer)
 */

#include "interfaces/http/controllers/dca_controller.h"
#include "application/dca/usecases.h"
#include "application/dca/commands.h"
#include "domain/dca/entity.h"
#include "infrastructure/repositories/dca_repo_impl.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
dca_service_list_plans(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    int64_t page = 1, page_size = 20;
    parse_page_params(c, &page, &page_size);
    const char* status = csilk_get_query(c, "status");

    int64_t       total = 0;
    csilk_json_t* list =
        dca_usecase_list_plans(db_get_pool(), user_id, page, page_size, status, &total);

    csilk_json_t* summary = dca_usecase_plan_summary(db_get_pool(), user_id);

    csilk_json_t* data = csilk_json_object();
    csilk_json_add_array(data, "list", list ? list : csilk_json_array());
    csilk_json_add_number(data, "total", (double)total);
    csilk_json_add_number(data, "page", (double)page);
    csilk_json_add_number(data, "page_size", (double)page_size);
    if (summary) {
        csilk_json_add_object(data, "summary", summary);
    }

    respond_ok(c, data);
}

void
dca_service_plan_summary(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* summary = dca_usecase_plan_summary(db_get_pool(), user_id);
    respond_ok(c, summary ? summary : csilk_json_object());
}

void
dca_service_create_plan(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id <= 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    create_dca_plan_cmd_t cmd = {
        .user_id = user_id,
        .target_asset_id = (int64_t)db_get_int(body, "target_asset_id"),
        .funding_asset_id = (int64_t)db_get_int(body, "funding_asset_id"),
        .name = csilk_json_get_string(body, "name"),
        .frequency = csilk_json_get_string(body, "frequency"),
        .day_of_period = (int)db_get_int(body, "day_of_period"),
        .amount = db_get_num(body, "amount"),
        .target_profit_rate = db_get_num(body, "target_profit_rate"),
        .target_total_amount = db_get_num(body, "target_total_amount"),
        .target_total_periods = (int)db_get_int(body, "target_total_periods"),
        .note = csilk_json_get_string(body, "note"),
    };

    dca_usecase_result_t res = {0};
    int64_t              new_id = dca_usecase_create_plan(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (new_id > 0 && res.code == 0) {
        csilk_json_t* r = csilk_json_object();
        csilk_json_add_number(r, "id", (double)new_id);
        respond_ok(c, r);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to create DCA plan");
    }
}

void
dca_service_get_plan(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = id_str ? atoll(id_str) : 0;

    dca_usecase_result_t res = {0};
    csilk_json_t*        detail = dca_usecase_get_plan(db_get_pool(), user_id, id, &res);

    if (detail) {
        respond_ok(c, detail);
    } else {
        respond_error(c, res.code ? res.code : 1003, res.message[0] ? res.message : "Not found");
    }
}

void
dca_service_update_plan(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = id_str ? atoll(id_str) : 0;

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    update_dca_plan_cmd_t cmd = {
        .user_id = user_id,
        .id = id,
        .target_asset_id = (int64_t)db_get_int(body, "target_asset_id"),
        .funding_asset_id = (int64_t)db_get_int(body, "funding_asset_id"),
        .name = csilk_json_get_string(body, "name"),
        .frequency = csilk_json_get_string(body, "frequency"),
        .day_of_period = (int)db_get_int(body, "day_of_period"),
        .amount = db_get_num(body, "amount"),
        .target_profit_rate = db_get_num(body, "target_profit_rate"),
        .target_total_amount = db_get_num(body, "target_total_amount"),
        .target_total_periods = (int)db_get_int(body, "target_total_periods"),
        .note = csilk_json_get_string(body, "note"),
    };

    dca_usecase_result_t res = {0};
    int                  rc = dca_usecase_update_plan(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to update DCA plan");
    }
}

void
dca_service_set_plan_status(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = id_str ? atoll(id_str) : 0;

    csilk_json_t* body = csilk_bind_json(c);
    const char*   status = body ? csilk_json_get_string(body, "status") : NULL;
    if (!status || !status[0]) {
        if (body) {
            csilk_json_free(body);
        }
        respond_bad_request(c, "Missing status");
        return;
    }

    dca_usecase_result_t res = {0};
    int                  rc = dca_usecase_set_plan_status(db_get_pool(), user_id, id, status, &res);
    if (body) {
        csilk_json_free(body);
    }

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to set DCA plan status");
    }
}

void
dca_service_delete_plan(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = id_str ? atoll(id_str) : 0;

    dca_usecase_result_t res = {0};
    int                  rc = dca_usecase_delete_plan(db_get_pool(), user_id, id, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to delete DCA plan");
    }
}

void
dca_service_list_executions(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     plan_id = id_str ? atoll(id_str) : 0;

    csilk_json_t* list = dca_usecase_list_executions(db_get_pool(), user_id, plan_id);
    respond_ok(c, list);
}

void
dca_service_list_pending_executions(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* list = dca_usecase_list_pending(db_get_pool(), user_id);
    respond_ok(c, list);
}

void
dca_service_confirm_execution(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     exec_id = id_str ? atoll(id_str) : 0;

    csilk_json_t* body = csilk_bind_json(c);

    confirm_dca_execution_cmd_t cmd = {
        .user_id = user_id,
        .exec_id = exec_id,
        .actual_amount = body ? db_get_num(body, "actual_amount") : 0.0,
        .executed_price = body ? db_get_num(body, "executed_price") : 0.0,
    };

    dca_confirm_result_t res = {0};
    int                  rc = dca_usecase_confirm_execution(db_get_pool(), &cmd, &res);
    if (body) {
        csilk_json_free(body);
    }

    if (rc == 0 && res.code == 0) {
        csilk_json_t* r = csilk_json_object();
        csilk_json_add_number(r, "transaction_id", (double)res.transaction_id);
        csilk_json_add_number(r, "actual_amount", res.actual_amount);
        csilk_json_add_number(r, "executed_price", res.executed_price);
        csilk_json_add_number(r, "executed_quantity", res.executed_quantity);
        respond_ok(c, r);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message[0] ? res.message : "定投执行失败");
    }
}

void
dca_service_skip_execution(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     exec_id = id_str ? atoll(id_str) : 0;

    dca_usecase_result_t res = {0};
    int                  rc = dca_usecase_skip_execution(db_get_pool(), user_id, exec_id, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(c,
                      res.code ? res.code : 1002,
                      res.message[0] ? res.message : "Failed to skip execution");
    }
}

/* Backward-compatibility aliases */
void
api_dca_list_plans(csilk_ctx_t* c)
{
    dca_service_list_plans(c);
}
void
api_dca_plan_summary(csilk_ctx_t* c)
{
    dca_service_plan_summary(c);
}
void
api_dca_create_plan(csilk_ctx_t* c)
{
    dca_service_create_plan(c);
}
void
api_dca_get_plan(csilk_ctx_t* c)
{
    dca_service_get_plan(c);
}
void
api_dca_update_plan(csilk_ctx_t* c)
{
    dca_service_update_plan(c);
}
void
api_dca_set_plan_status(csilk_ctx_t* c)
{
    dca_service_set_plan_status(c);
}
void
api_dca_delete_plan(csilk_ctx_t* c)
{
    dca_service_delete_plan(c);
}
void
api_dca_list_executions(csilk_ctx_t* c)
{
    dca_service_list_executions(c);
}
void
api_dca_list_pending_executions(csilk_ctx_t* c)
{
    dca_service_list_pending_executions(c);
}
void
api_dca_confirm_execution(csilk_ctx_t* c)
{
    dca_service_confirm_execution(c);
}
void
api_dca_skip_execution(csilk_ctx_t* c)
{
    dca_service_skip_execution(c);
}

void
register_dca_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/dca/plans",
                      dca_service_list_plans,
                      NULL,
                      NULL,
                      "List DCA plans",
                      "Get user's DCA plans with pagination");
    csilk_app_get_ext(app,
                      "/api/dca/plans/summary",
                      dca_service_plan_summary,
                      NULL,
                      NULL,
                      "Get DCA plans summary",
                      "Get overall DCA statistics and metrics");
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
