#include "services/dca_service.h"
#include "repositories/dca_repo.h"
#include "repositories/transaction_repo.h"
#include "common/balance.h"
#include "common/ctx.h"
#include "common/response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
dca_service_list_plans(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();

    csilk_json_t* list = dca_plan_list(pool, user_id);
    if (!list) {
        respond_ok(c, csilk_json_array());
        return;
    }

    size_t count = csilk_json_array_size(list);
    for (size_t i = 0; i < count; ++i) {
        csilk_json_t* item = csilk_json_array_get(list, i);
        double        target_curr_val = db_get_num(item, "target_current_value");
        double        total_invested = db_get_num(item, "total_invested_amount");
        double        target_profit_rate = db_get_num(item, "target_profit_rate");

        double profit_rate = 0.0;
        if (total_invested > 0.0) {
            profit_rate = (target_curr_val - total_invested) / total_invested;
        }
        bool profit_target_reached =
            (target_profit_rate > 0.0 && profit_rate >= target_profit_rate);

        csilk_json_add_number(item, "profit_rate", profit_rate);
        csilk_json_add_bool(item, "profit_target_reached", profit_target_reached);
    }

    respond_ok(c, list);
}

void
dca_service_create_plan(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    body = csilk_bind_json(c);

    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    int64_t     target_asset_id = (int64_t)db_get_int(body, "target_asset_id");
    int64_t     funding_asset_id = (int64_t)db_get_int(body, "funding_asset_id");
    const char* name = csilk_json_get_string(body, "name");
    const char* frequency = csilk_json_get_string(body, "frequency");
    int         day_of_period = (int)db_get_int(body, "day_of_period");
    double      amount = db_get_num(body, "amount");
    double      target_profit_rate = db_get_num(body, "target_profit_rate");
    double      target_total_amount = db_get_num(body, "target_total_amount");
    int         target_total_periods = (int)db_get_int(body, "target_total_periods");
    const char* note = csilk_json_get_string(body, "note");

    if (target_asset_id <= 0 || funding_asset_id <= 0 || !name || !name[0] || amount <= 0.0) {
        respond_bad_request(
            c, "Missing required plan fields (target_asset_id, funding_asset_id, name, amount)");
        return;
    }

    int64_t new_id = dca_plan_create(pool,
                                     user_id,
                                     target_asset_id,
                                     funding_asset_id,
                                     name,
                                     frequency,
                                     day_of_period,
                                     amount,
                                     target_profit_rate,
                                     target_total_amount,
                                     target_total_periods,
                                     note);
    if (new_id <= 0) {
        respond_error(c, 1002, "Failed to create DCA plan");
        return;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "id", (double)new_id);
    respond_ok(c, res);
}

void
dca_service_get_plan(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();
    const char*      id_str = csilk_get_param(c, "id");
    int64_t          id = id_str ? atoll(id_str) : 0;

    csilk_json_t* res = dca_plan_get(pool, user_id, id);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        respond_not_found(c);
        return;
    }

    csilk_json_t* item = csilk_json_array_get(res, 0);
    double        target_curr_val = db_get_num(item, "target_current_value");
    double        total_invested = db_get_num(item, "total_invested_amount");
    double        target_profit_rate = db_get_num(item, "target_profit_rate");
    double        profit_rate =
        (total_invested > 0.0) ? (target_curr_val - total_invested) / total_invested : 0.0;
    bool profit_target_reached = (target_profit_rate > 0.0 && profit_rate >= target_profit_rate);

    csilk_json_add_number(item, "profit_rate", profit_rate);
    csilk_json_add_bool(item, "profit_target_reached", profit_target_reached);

    respond_ok(c, res);
}

void
dca_service_update_plan(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();
    const char*      id_str = csilk_get_param(c, "id");
    int64_t          id = id_str ? atoll(id_str) : 0;
    csilk_json_t*    body = csilk_bind_json(c);

    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    int64_t     target_asset_id = (int64_t)db_get_int(body, "target_asset_id");
    int64_t     funding_asset_id = (int64_t)db_get_int(body, "funding_asset_id");
    const char* name = csilk_json_get_string(body, "name");
    const char* frequency = csilk_json_get_string(body, "frequency");
    int         day_of_period = (int)db_get_int(body, "day_of_period");
    double      amount = db_get_num(body, "amount");
    double      target_profit_rate = db_get_num(body, "target_profit_rate");
    double      target_total_amount = db_get_num(body, "target_total_amount");
    int         target_total_periods = (int)db_get_int(body, "target_total_periods");
    const char* note = csilk_json_get_string(body, "note");

    int ret = dca_plan_update(pool,
                              user_id,
                              id,
                              target_asset_id,
                              funding_asset_id,
                              name,
                              frequency,
                              day_of_period,
                              amount,
                              target_profit_rate,
                              target_total_amount,
                              target_total_periods,
                              note);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to update DCA plan");
        return;
    }
    respond_ok(c, NULL);
}

void
dca_service_set_plan_status(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();
    const char*      id_str = csilk_get_param(c, "id");
    int64_t          id = id_str ? atoll(id_str) : 0;
    csilk_json_t*    body = csilk_bind_json(c);

    const char* status = body ? csilk_json_get_string(body, "status") : NULL;
    if (!status || !status[0]) {
        respond_bad_request(c, "Missing status");
        return;
    }

    int ret = dca_plan_set_status(pool, user_id, id, status);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to set DCA plan status");
        return;
    }
    respond_ok(c, NULL);
}

void
dca_service_delete_plan(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();
    const char*      id_str = csilk_get_param(c, "id");
    int64_t          id = id_str ? atoll(id_str) : 0;

    int ret = dca_plan_delete(pool, user_id, id);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to delete DCA plan");
        return;
    }
    respond_ok(c, NULL);
}

void
dca_service_list_executions(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();
    const char*      id_str = csilk_get_param(c, "id");
    int64_t          plan_id = id_str ? atoll(id_str) : 0;

    csilk_json_t* list = dca_execution_list_by_plan(pool, user_id, plan_id);
    respond_ok(c, list ? list : csilk_json_array());
}

void
dca_service_list_pending_executions(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();

    csilk_json_t* list = dca_execution_list_pending(pool, user_id);
    respond_ok(c, list ? list : csilk_json_array());
}

void
dca_service_confirm_execution(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();
    const char*      id_str = csilk_get_param(c, "id");
    int64_t          exec_id = id_str ? atoll(id_str) : 0;
    csilk_json_t*    body = csilk_bind_json(c);

    csilk_json_t* exec_arr = dca_execution_get(pool, user_id, exec_id);
    if (!exec_arr || csilk_json_array_size(exec_arr) == 0) {
        if (exec_arr) {
            csilk_json_free(exec_arr);
        }
        respond_not_found(c);
        return;
    }

    csilk_json_t* exec = csilk_json_array_get(exec_arr, 0);
    const char*   status = csilk_json_get_string(exec, "status");
    if (status && strcmp(status, "pending") != 0) {
        csilk_json_free(exec_arr);
        respond_bad_request(c, "Task is not in pending status");
        return;
    }

    int64_t     target_asset_id = (int64_t)db_get_int(exec, "target_asset_id");
    int64_t     funding_asset_id = (int64_t)db_get_int(exec, "funding_asset_id");
    double      planned_amount = db_get_num(exec, "planned_amount");
    double      target_net_val = db_get_num(exec, "target_net_value");
    const char* period_date = csilk_json_get_string(exec, "period_date");

    double actual_amount = body ? db_get_num(body, "actual_amount") : 0.0;
    if (actual_amount <= 0.0) {
        actual_amount = planned_amount;
    }
    double executed_price = body ? db_get_num(body, "executed_price") : 0.0;
    if (executed_price <= 0.0) {
        executed_price = target_net_val > 0.0 ? target_net_val : 1.0;
    }
    double executed_quantity = actual_amount / executed_price;

    /* Execute Transaction within BEGIN / COMMIT block */
    csilk_db_exec(pool, "BEGIN TRANSACTION");

    char tx_date[32];
    snprintf(tx_date, sizeof(tx_date), "%s 09:30:00", period_date ? period_date : "2026-08-28");

    int64_t tx_id = tx_insert(pool,
                              user_id,
                              target_asset_id,
                              funding_asset_id,
                              0, /* category_id */
                              "expense",
                              "buy",
                              "in",
                              "out",
                              actual_amount,
                              executed_price,
                              executed_quantity,
                              0.0, /* fee */
                              "CNY",
                              tx_date,
                              "定投计划自动买入");
    if (tx_id <= 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(exec_arr);
        respond_error(c, 1002, "Failed to create transaction for DCA execution");
        return;
    }

    /* Apply position to target asset */
    double pos_delta = 0.0;
    apply_position(pool,
                   target_asset_id,
                   "buy",
                   actual_amount,
                   0.0,
                   executed_price,
                   executed_quantity,
                   &pos_delta);

    if (balance_apply_delta(
            pool, funding_asset_id, user_id, -actual_amount, "transaction", tx_id, "定投扣款") !=
        0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(exec_arr);
        respond_error(c, 1002, "定投扣款失败");
        return;
    }

    /* Update execution record */
    dca_execution_update_confirmed(
        pool, exec_id, actual_amount, executed_price, executed_quantity, tx_id);

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(exec_arr);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "transaction_id", (double)tx_id);
    csilk_json_add_number(res, "actual_amount", actual_amount);
    csilk_json_add_number(res, "executed_price", executed_price);
    csilk_json_add_number(res, "executed_quantity", executed_quantity);
    respond_ok(c, res);
}

void
dca_service_skip_execution(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();
    const char*      id_str = csilk_get_param(c, "id");
    int64_t          exec_id = id_str ? atoll(id_str) : 0;

    int ret = dca_execution_update_status(pool, user_id, exec_id, "skipped");
    if (ret != 0) {
        respond_error(c, 1002, "Failed to skip execution");
        return;
    }
    respond_ok(c, NULL);
}
