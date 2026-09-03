#include "interfaces/http/controllers/cashflow_controller.h"
#include "application/cashflow/usecases.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
api_cashflow_list_schedules(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t*             list = NULL;
    cashflow_usecase_result_t res = {0};
    cashflow_usecase_list_schedules(db_get_pool(), user_id, &list, &res);
    respond_ok(c, list ? list : csilk_json_array());
}

void
api_cashflow_create_schedule(csilk_ctx_t* c)
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

    create_cashflow_cmd_t cmd = {
        .user_id = user_id,
        .source_asset_id = (int64_t)db_get_int(body, "source_asset_id"),
        .target_asset_id = (int64_t)db_get_int(body, "target_asset_id"),
        .name = csilk_json_get_string(body, "name"),
        .flow_type = csilk_json_get_string(body, "flow_type"),
        .frequency = csilk_json_get_string(body, "frequency"),
        .start_date = csilk_json_get_string(body, "start_date"),
        .end_date = csilk_json_get_string(body, "end_date"),
        .expected_amount = db_get_num(body, "expected_amount"),
        .note = csilk_json_get_string(body, "note"),
    };

    int64_t                   new_id = 0;
    cashflow_usecase_result_t res = {0};
    int rc = cashflow_usecase_create_schedule(db_get_pool(), &cmd, &new_id, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* out = csilk_json_object();
        csilk_json_add_number(out, "id", (double)new_id);
        respond_ok(c, out);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
api_cashflow_get_schedule(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* id_str = csilk_get_param(c, "id");
    int64_t     id = id_str ? atoll(id_str) : 0;

    csilk_json_t*             item = NULL;
    cashflow_usecase_result_t res = {0};
    int rc = cashflow_usecase_get_schedule(db_get_pool(), user_id, id, &item, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, item);
    } else if (res.code == 1003) {
        respond_not_found(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_cashflow_update_schedule(csilk_ctx_t* c)
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

    update_cashflow_cmd_t cmd = {
        .user_id = user_id,
        .id = id,
        .source_asset_id = (int64_t)db_get_int(body, "source_asset_id"),
        .target_asset_id = (int64_t)db_get_int(body, "target_asset_id"),
        .name = csilk_json_get_string(body, "name"),
        .flow_type = csilk_json_get_string(body, "flow_type"),
        .frequency = csilk_json_get_string(body, "frequency"),
        .start_date = csilk_json_get_string(body, "start_date"),
        .end_date = csilk_json_get_string(body, "end_date"),
        .expected_amount = db_get_num(body, "expected_amount"),
        .note = csilk_json_get_string(body, "note"),
    };

    cashflow_usecase_result_t res = {0};
    int                       rc = cashflow_usecase_update_schedule(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
api_cashflow_delete_schedule(csilk_ctx_t* c)
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

    cashflow_usecase_result_t res = {0};
    int rc = cashflow_usecase_delete_schedule(db_get_pool(), user_id, id, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, NULL);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
api_cashflow_get_calendar(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");

    query_calendar_cmd_t cmd = {
        .user_id = user_id,
        .year = year_str ? atoi(year_str) : 0,
        .month = month_str ? atoi(month_str) : 0,
    };

    csilk_json_t*             resp = NULL;
    cashflow_usecase_result_t res = {0};
    int                       rc = cashflow_usecase_get_calendar(db_get_pool(), &cmd, &resp, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok(c, resp);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_cashflow_confirm(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    double                 amt = db_get_num(body, "amount");
    confirm_cashflow_cmd_t cmd = {
        .user_id = user_id,
        .source_asset_id = (int64_t)db_get_int(body, "source_asset_id"),
        .target_asset_id = (int64_t)db_get_int(body, "target_asset_id"),
        .amount = amt,
        .date = csilk_json_get_string(body, "date"),
        .name = csilk_json_get_string(body, "name"),
        .note = csilk_json_get_string(body, "note"),
    };

    int64_t                   tx_id = 0;
    cashflow_usecase_result_t res = {0};
    int rc = cashflow_usecase_confirm_income(db_get_pool(), &cmd, &tx_id, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* out = csilk_json_object();
        csilk_json_add_number(out, "transaction_id", (double)tx_id);
        csilk_json_add_number(out, "amount", amt);
        respond_ok(c, out);
    } else {
        respond_error(c, res.code ? res.code : 1002, res.message);
    }
}

void
register_cashflow_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/cashflow/schedules",
                      api_cashflow_list_schedules,
                      NULL,
                      NULL,
                      "List cashflow schedules",
                      "Returns all passive cashflow schedules for user");
    csilk_app_post_ext(app,
                       "/api/cashflow/schedules",
                       api_cashflow_create_schedule,
                       NULL,
                       NULL,
                       "Create cashflow schedule",
                       "Creates a new passive cashflow schedule");
    csilk_app_get_ext(app,
                      "/api/cashflow/schedules/:id",
                      api_cashflow_get_schedule,
                      NULL,
                      NULL,
                      "Get cashflow schedule",
                      "Returns single schedule details");
    csilk_app_put_ext(app,
                      "/api/cashflow/schedules/:id",
                      api_cashflow_update_schedule,
                      NULL,
                      NULL,
                      "Update cashflow schedule",
                      "Updates schedule configuration");
    csilk_app_delete_ext(app,
                         "/api/cashflow/schedules/:id",
                         api_cashflow_delete_schedule,
                         NULL,
                         NULL,
                         "Delete cashflow schedule",
                         "Deletes schedule");
    csilk_app_get_ext(app,
                      "/api/cashflow/calendar",
                      api_cashflow_get_calendar,
                      NULL,
                      NULL,
                      "Cashflow calendar",
                      "Returns calendar projections and actual events for month");
    csilk_app_post_ext(app,
                       "/api/cashflow/confirm",
                       api_cashflow_confirm,
                       NULL,
                       NULL,
                       "Confirm income",
                       "Confirms passive income received and logs transaction");
}
