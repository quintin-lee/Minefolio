/**
 * @file usecases.c
 * @brief 定投计划用例编排实现 (Application DCA Use Cases)
 */

#include "application/dca/usecases.h"
#include "domain/dca/rules.h"
#include "domain/dca/repository.h"
#include "infrastructure/repositories/dca_repo_impl.h"
#include "core/ledger/ledger_engine.h"
#include "common/db.h"
#include "common/response.h"
#include <stdio.h>
#include <string.h>

/* ===== Internal helpers ===== */

/**
 * @brief 向 JSON 对象添加收益率和止盈状态
 */
static void
add_profit_fields(csilk_json_t* item)
{
    double target_curr_val = db_get_num(item, "target_current_value");
    double total_invested = db_get_num(item, "total_invested_amount");
    double target_profit_rate = db_get_num(item, "target_profit_rate");

    double profit_rate = mf_dca_rule_calc_profit_rate(target_curr_val, total_invested);
    bool   profit_target_reached = mf_dca_rule_check_profit_target(profit_rate, target_profit_rate);

    csilk_json_add_number(item, "profit_rate", profit_rate);
    csilk_json_add_bool(item, "profit_target_reached", profit_target_reached);
}

/* ===== Use case implementations ===== */

csilk_json_t*
dca_usecase_list_plans(void* pool, int64_t user_id)
{
    csilk_json_t* list = mf_dca_repo_plan_list(pool, user_id);
    if (!list) {
        return csilk_json_array();
    }

    size_t count = csilk_json_array_size(list);
    for (size_t i = 0; i < count; i++) {
        add_profit_fields(csilk_json_array_get(list, i));
    }
    return list;
}

csilk_json_t*
dca_usecase_get_plan(void* pool, int64_t user_id, int64_t id, dca_usecase_result_t* out_res)
{
    csilk_json_t* res = mf_dca_repo_plan_get(pool, user_id, id);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "Plan not found");
        return NULL;
    }

    add_profit_fields(csilk_json_array_get(res, 0));
    return res;
}

int64_t
dca_usecase_create_plan(void* pool, const create_dca_plan_cmd_t* cmd, dca_usecase_result_t* out_res)
{
    if (!mf_dca_rule_validate_plan_required(
            cmd->target_asset_id, cmd->funding_asset_id, cmd->name, cmd->amount)) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "Missing required plan fields (target_asset_id, funding_asset_id, name, amount)");
        return -1;
    }

    mf_dca_plan_t plan = {0};
    plan.user_id = cmd->user_id;
    plan.target_asset_id = cmd->target_asset_id;
    plan.funding_asset_id = cmd->funding_asset_id;
    strncpy(plan.name, cmd->name, sizeof(plan.name) - 1);
    if (cmd->frequency) {
        strncpy(plan.frequency, cmd->frequency, sizeof(plan.frequency) - 1);
    }
    plan.day_of_period = cmd->day_of_period;
    plan.amount = cmd->amount;
    plan.target_profit_rate = cmd->target_profit_rate;
    plan.target_total_amount = cmd->target_total_amount;
    plan.target_total_periods = cmd->target_total_periods;
    if (cmd->note) {
        strncpy(plan.note, cmd->note, sizeof(plan.note) - 1);
    }

    int64_t id = mf_dca_repo_plan_create(pool, cmd->user_id, &plan);
    if (id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to create DCA plan");
    }
    return id;
}

int
dca_usecase_update_plan(void* pool, const update_dca_plan_cmd_t* cmd, dca_usecase_result_t* out_res)
{
    mf_dca_plan_t plan = {0};
    plan.target_asset_id = cmd->target_asset_id;
    plan.funding_asset_id = cmd->funding_asset_id;
    if (cmd->name) {
        strncpy(plan.name, cmd->name, sizeof(plan.name) - 1);
    }
    if (cmd->frequency) {
        strncpy(plan.frequency, cmd->frequency, sizeof(plan.frequency) - 1);
    }
    plan.day_of_period = cmd->day_of_period;
    plan.amount = cmd->amount;
    plan.target_profit_rate = cmd->target_profit_rate;
    plan.target_total_amount = cmd->target_total_amount;
    plan.target_total_periods = cmd->target_total_periods;
    if (cmd->note) {
        strncpy(plan.note, cmd->note, sizeof(plan.note) - 1);
    }

    int rc = mf_dca_repo_plan_update(pool, cmd->user_id, cmd->id, &plan);
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to update DCA plan");
    }
    return rc;
}

int
dca_usecase_set_plan_status(
    void* pool, int64_t user_id, int64_t id, const char* status, dca_usecase_result_t* out_res)
{
    if (!mf_dca_rule_validate_status(status)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Invalid status");
        return -1;
    }

    int rc = mf_dca_repo_plan_set_status(pool, user_id, id, status);
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to set DCA plan status");
    }
    return rc;
}

int
dca_usecase_delete_plan(void* pool, int64_t user_id, int64_t id, dca_usecase_result_t* out_res)
{
    int rc = mf_dca_repo_plan_delete(pool, user_id, id);
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to delete DCA plan");
    }
    return rc;
}

csilk_json_t*
dca_usecase_list_executions(void* pool, int64_t user_id, int64_t plan_id)
{
    csilk_json_t* list = mf_dca_repo_execution_list_by_plan(pool, user_id, plan_id);
    return list ? list : csilk_json_array();
}

csilk_json_t*
dca_usecase_list_pending(void* pool, int64_t user_id)
{
    csilk_json_t* list = mf_dca_repo_execution_list_pending(pool, user_id);
    return list ? list : csilk_json_array();
}

int
dca_usecase_confirm_execution(void*                              pool,
                              const confirm_dca_execution_cmd_t* cmd,
                              dca_confirm_result_t*              out_res)
{
    csilk_json_t* exec_arr = mf_dca_repo_execution_get(pool, cmd->user_id, cmd->exec_id);
    if (!exec_arr || csilk_json_array_size(exec_arr) == 0) {
        if (exec_arr) {
            csilk_json_free(exec_arr);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "Execution not found");
        return -1;
    }

    csilk_json_t* exec = csilk_json_array_get(exec_arr, 0);
    const char*   status = csilk_json_get_string(exec, "status");
    if (status && strcmp(status, "pending") != 0) {
        csilk_json_free(exec_arr);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Task is not in pending status");
        return -1;
    }

    int64_t     target_asset_id = (int64_t)db_get_int(exec, "target_asset_id");
    int64_t     funding_asset_id = (int64_t)db_get_int(exec, "funding_asset_id");
    double      planned_amount = db_get_num(exec, "planned_amount");
    double      target_net_val = db_get_num(exec, "target_net_value");
    const char* period_date = csilk_json_get_string(exec, "period_date");

    double actual_amount = cmd->actual_amount > 0.0 ? cmd->actual_amount : planned_amount;
    double executed_price = cmd->executed_price > 0.0
                                ? cmd->executed_price
                                : (target_net_val > 0.0 ? target_net_val : 1.0);

    /* Calculate quantity */
    money_t    actual_m;
    price_t    price_p;
    quantity_t qty_q;
    money_from_double(actual_amount, CURRENCY_CNY, &actual_m);
    price_from_double(executed_price, 4, CURRENCY_CNY, &price_p);
    money_div_price(actual_m, price_p, 8, ROUND_HALF_UP, &qty_q);
    double executed_quantity = quantity_to_double(qty_q);

    /* Execute Transaction within BEGIN / COMMIT block */
    csilk_db_exec(pool, "BEGIN TRANSACTION");

    char tx_date[32];
    snprintf(tx_date, sizeof(tx_date), "%s 09:30:00", period_date ? period_date : "2026-08-28");

    ledger_tx_t ltx = {.id = 0,
                       .user_id = cmd->user_id,
                       .asset_id = target_asset_id,
                       .linked_asset_id = funding_asset_id,
                       .category_id = 0,
                       .type = LEDGER_TX_BUY,
                       .type_str = "buy",
                       .amount = actual_m,
                       .price = price_p,
                       .quantity = qty_q,
                       .fee = money_zero(CURRENCY_CNY),
                       .tx_date = tx_date,
                       .note = "定投计划自动买入",
                       .parent_tx_id = 0};

    if (ledger_apply_tx(pool, &ltx) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(exec_arr);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "定投执行失败");
        return -1;
    }

    int64_t tx_id = ltx.id;

    /* Update execution record */
    mf_dca_repo_execution_update_confirmed(
        pool, cmd->exec_id, actual_amount, executed_price, executed_quantity, tx_id);

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(exec_arr);

    out_res->code = 0;
    out_res->transaction_id = tx_id;
    out_res->actual_amount = actual_amount;
    out_res->executed_price = executed_price;
    out_res->executed_quantity = executed_quantity;
    return 0;
}

int
dca_usecase_skip_execution(void*                 pool,
                           int64_t               user_id,
                           int64_t               exec_id,
                           dca_usecase_result_t* out_res)
{
    int rc = mf_dca_repo_execution_update_status(pool, user_id, exec_id, "skipped");
    if (rc != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "Failed to skip execution");
    }
    return rc;
}
