#include "application/cashflow/usecases.h"
#include "domain/cashflow/repository.h"
#include "domain/cashflow/rules.h"
#include "repositories/cashflow_repo.h"
#include "repositories/transaction_repo.h"
#include "common/balance.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int
cashflow_usecase_list_schedules(void*                      pool,
                                int64_t                    user_id,
                                csilk_json_t**             out_list,
                                cashflow_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !out_list) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    csilk_json_t* list = cashflow_schedule_list((csilk_db_pool_t*)pool, user_id);
    *out_list = list ? list : csilk_json_array();
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
cashflow_usecase_get_schedule(void*                      pool,
                              int64_t                    user_id,
                              int64_t                    id,
                              csilk_json_t**             out_item,
                              cashflow_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !out_item || id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    csilk_json_t* res = cashflow_schedule_get((csilk_db_pool_t*)pool, user_id, id);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        out_res->code = 1003;
        snprintf(out_res->message, sizeof(out_res->message), "现金流计划不存在");
        return -1;
    }

    *out_item = res;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
cashflow_usecase_create_schedule(void*                        pool,
                                 const create_cashflow_cmd_t* cmd,
                                 int64_t*                     out_id,
                                 cashflow_usecase_result_t*   out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    mf_cashflow_schedule_t s = {0};
    s.user_id = cmd->user_id;
    s.source_asset_id = cmd->source_asset_id;
    s.target_asset_id = cmd->target_asset_id;
    if (cmd->name) {
        snprintf(s.name, sizeof(s.name), "%s", cmd->name);
    }
    if (cmd->flow_type) {
        snprintf(s.flow_type, sizeof(s.flow_type), "%s", cmd->flow_type);
    }
    if (cmd->frequency) {
        snprintf(s.frequency, sizeof(s.frequency), "%s", cmd->frequency);
    }
    if (cmd->start_date) {
        snprintf(s.start_date, sizeof(s.start_date), "%s", cmd->start_date);
    }
    if (cmd->end_date) {
        snprintf(s.end_date, sizeof(s.end_date), "%s", cmd->end_date);
    }
    if (cmd->note) {
        snprintf(s.note, sizeof(s.note), "%s", cmd->note);
    }
    currency_t cny = currency_from_str("CNY");
    money_from_double(cmd->expected_amount, cny, &s.expected_amount);

    char err[256];
    if (mf_cashflow_rule_validate(&s, err, sizeof(err)) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "%s", err);
        return -1;
    }

    int64_t new_id = 0;
    if (mf_cashflow_repo_create(pool, &s, &new_id) != 0 || new_id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "创建现金流计划失败");
        return -1;
    }

    if (out_id) {
        *out_id = new_id;
    }
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
cashflow_usecase_update_schedule(void*                        pool,
                                 const update_cashflow_cmd_t* cmd,
                                 cashflow_usecase_result_t*   out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || cmd->id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    mf_cashflow_schedule_t s = {0};
    s.id = cmd->id;
    s.user_id = cmd->user_id;
    s.source_asset_id = cmd->source_asset_id;
    s.target_asset_id = cmd->target_asset_id;
    if (cmd->name) {
        snprintf(s.name, sizeof(s.name), "%s", cmd->name);
    }
    if (cmd->flow_type) {
        snprintf(s.flow_type, sizeof(s.flow_type), "%s", cmd->flow_type);
    }
    if (cmd->frequency) {
        snprintf(s.frequency, sizeof(s.frequency), "%s", cmd->frequency);
    }
    if (cmd->start_date) {
        snprintf(s.start_date, sizeof(s.start_date), "%s", cmd->start_date);
    }
    if (cmd->end_date) {
        snprintf(s.end_date, sizeof(s.end_date), "%s", cmd->end_date);
    }
    if (cmd->note) {
        snprintf(s.note, sizeof(s.note), "%s", cmd->note);
    }
    currency_t cny = currency_from_str("CNY");
    money_from_double(cmd->expected_amount, cny, &s.expected_amount);

    char err[256];
    if (mf_cashflow_rule_validate(&s, err, sizeof(err)) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "%s", err);
        return -1;
    }

    if (mf_cashflow_repo_update(pool, &s) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "更新现金流计划失败");
        return -1;
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
cashflow_usecase_delete_schedule(void*                      pool,
                                 int64_t                    user_id,
                                 int64_t                    id,
                                 cashflow_usecase_result_t* out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || id <= 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    if (mf_cashflow_repo_delete(pool, user_id, id) != 0) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "删除现金流计划失败");
        return -1;
    }

    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
cashflow_usecase_get_calendar(void*                       pool,
                              const query_calendar_cmd_t* cmd,
                              csilk_json_t**              out_resp,
                              cashflow_usecase_result_t*  out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd || !out_resp) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    int       year = cmd->year;
    int       month = cmd->month;
    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (year < 2000 || year > 2100) {
        year = tm_now.tm_year + 1900;
    }
    if (month < 1 || month > 12) {
        month = tm_now.tm_mon + 1;
    }

    char ym[32];
    snprintf(ym, sizeof(ym), "%04d-%02d", year, month);

    csilk_json_t* events_arr = csilk_json_array();
    double        actual_total = 0.0;
    double        projected_total = 0.0;
    double        annual_projected_total = 0.0;

    /* 1. 查询真实历史已确认入账 */
    mf_cashflow_event_t* actual_evs = NULL;
    size_t               actual_count = 0;
    if (mf_cashflow_repo_get_actual_events(pool, cmd->user_id, ym, &actual_evs, &actual_count) ==
            0 &&
        actual_evs) {
        for (size_t i = 0; i < actual_count; i++) {
            double amt = money_to_double(actual_evs[i].amount);
            actual_total += amt;

            csilk_json_t* ev = csilk_json_object();
            csilk_json_add_number(ev, "id", (double)actual_evs[i].id);
            csilk_json_add_string(ev, "date", actual_evs[i].date);
            csilk_json_add_string(ev, "name", actual_evs[i].name);
            csilk_json_add_string(ev, "flow_type", actual_evs[i].flow_type);
            csilk_json_add_number(ev, "amount", amt);
            csilk_json_add_string(ev, "currency", currency_code(&actual_evs[i].currency));
            csilk_json_add_bool(ev, "is_actual", true);
            csilk_json_add_string(ev, "status", actual_evs[i].status);
            csilk_json_array_append(events_arr, ev);
        }
        mf_cashflow_repo_free_events(actual_evs, actual_count);
    }

    /* 2. 推算未来排程发生 */
    mf_cashflow_schedule_t* schedules = NULL;
    size_t                  sch_count = 0;
    if (mf_cashflow_repo_list_active(pool, cmd->user_id, &schedules, &sch_count) == 0 &&
        schedules) {
        for (size_t i = 0; i < sch_count; i++) {
            const mf_cashflow_schedule_t* sch = &schedules[i];
            double                        exp_amt = money_to_double(sch->expected_amount);

            double factor = 12.0;
            mf_cashflow_rule_annual_factor(sch->frequency, &factor);
            annual_projected_total += exp_amt * factor;

            int target_day = 0;
            if (mf_cashflow_rule_matches_month(
                    sch->frequency, sch->start_date, sch->end_date, year, month, &target_day)) {
                projected_total += exp_amt;
                char pdate[64];
                snprintf(pdate, sizeof(pdate), "%s-%02d", ym, target_day);

                csilk_json_t* ev = csilk_json_object();
                csilk_json_add_number(ev, "schedule_id", (double)sch->id);
                csilk_json_add_number(ev, "source_asset_id", (double)sch->source_asset_id);
                csilk_json_add_number(ev, "target_asset_id", (double)sch->target_asset_id);
                csilk_json_add_string(ev, "date", pdate);
                csilk_json_add_string(ev, "name", sch->name[0] ? sch->name : "预期收益");
                csilk_json_add_string(
                    ev, "flow_type", sch->flow_type[0] ? sch->flow_type : "dividend");
                csilk_json_add_number(ev, "amount", exp_amt);
                csilk_json_add_string(
                    ev, "currency", currency_code(&sch->expected_amount.currency));
                csilk_json_add_bool(ev, "is_actual", false);
                csilk_json_add_string(ev, "status", "projected");
                csilk_json_array_append(events_arr, ev);
            }
        }
        mf_cashflow_repo_free_list(schedules, sch_count);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "year", (double)year);
    csilk_json_add_number(res, "month", (double)month);
    csilk_json_add_string(res, "year_month", ym);
    csilk_json_add_number(res, "actual_total", actual_total);
    csilk_json_add_number(res, "projected_total", projected_total);
    csilk_json_add_number(res, "annual_projected_total", annual_projected_total);
    csilk_json_add_array(res, "events", events_arr);

    *out_resp = res;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}

int
cashflow_usecase_confirm_income(void*                         pool,
                                const confirm_cashflow_cmd_t* cmd,
                                int64_t*                      out_tx_id,
                                cashflow_usecase_result_t*    out_res)
{
    if (!out_res) {
        return -1;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!pool || !cmd) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "参数错误");
        return -1;
    }

    if (cmd->target_asset_id <= 0 || cmd->amount <= 0.0 || !cmd->date || !cmd->date[0]) {
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "Missing required fields (target_asset_id, amount, date)");
        return -1;
    }

    char tx_note[256];
    snprintf(tx_note,
             sizeof(tx_note),
             "%s %s",
             cmd->name && cmd->name[0] ? cmd->name : "现金流收益入账",
             cmd->note && cmd->note[0] ? cmd->note : "");

    char full_date[32];
    snprintf(full_date, sizeof(full_date), "%s 10:00:00", cmd->date);

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    db_tx_scope_t    scope;
    if (db_tx_scope_begin(db_pool, "mf_cashflow_confirm", &scope) != 0) {
        out_res->code = 1004;
        snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
        return -1;
    }

    int64_t tx_id = tx_insert(db_pool,
                              cmd->user_id,
                              cmd->target_asset_id,
                              cmd->source_asset_id,
                              0,
                              "income",
                              "income",
                              "in",
                              "out",
                              cmd->amount,
                              0.0,
                              0.0,
                              0.0,
                              "CNY",
                              full_date,
                              tx_note);
    if (tx_id <= 0) {
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 1002;
        snprintf(out_res->message,
                 sizeof(out_res->message),
                 "Failed to create transaction for cash flow income");
        return -1;
    }

    if (balance_apply_delta(db_pool,
                            cmd->target_asset_id,
                            cmd->user_id,
                            cmd->amount,
                            "transaction",
                            tx_id,
                            tx_note) != 0) {
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "现金流余额更新失败");
        return -1;
    }

    db_tx_scope_commit(db_pool, &scope);

    if (out_tx_id) {
        *out_tx_id = tx_id;
    }
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}
