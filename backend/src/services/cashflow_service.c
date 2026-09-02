#include "services/cashflow_service.h"
#include "repositories/cashflow_repo.h"
#include "repositories/transaction_repo.h"
#include "common/balance.h"
#include "common/ctx.h"
#include "common/response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void
cashflow_service_list_schedules(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();

    csilk_json_t* list = cashflow_schedule_list(pool, user_id);
    respond_ok(c, list ? list : csilk_json_array());
}

void
cashflow_service_create_schedule(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id <= 0) {
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    body = csilk_bind_json(c);

    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    int64_t     source_asset_id = (int64_t)db_get_int(body, "source_asset_id");
    int64_t     target_asset_id = (int64_t)db_get_int(body, "target_asset_id");
    const char* name = csilk_json_get_string(body, "name");
    const char* flow_type = csilk_json_get_string(body, "flow_type");
    const char* frequency = csilk_json_get_string(body, "frequency");
    const char* start_date = csilk_json_get_string(body, "start_date");
    const char* end_date = csilk_json_get_string(body, "end_date");
    double      expected_amount = db_get_num(body, "expected_amount");
    const char* note = csilk_json_get_string(body, "note");

    if (source_asset_id <= 0 || target_asset_id <= 0 || !name || !name[0] || !start_date ||
        expected_amount <= 0.0) {
        respond_bad_request(c,
                            "Missing required schedule fields (source_asset_id, target_asset_id, "
                            "name, start_date, expected_amount)");
        return;
    }

    int64_t new_id = cashflow_schedule_create(pool,
                                              user_id,
                                              source_asset_id,
                                              target_asset_id,
                                              name,
                                              flow_type,
                                              frequency,
                                              start_date,
                                              end_date,
                                              expected_amount,
                                              note);
    if (new_id <= 0) {
        respond_error(c, 1002, "Failed to create cashflow schedule");
        return;
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "id", (double)new_id);
    respond_ok(c, res);
}

void
cashflow_service_get_schedule(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();
    const char*      id_str = csilk_get_param(c, "id");
    int64_t          id = id_str ? atoll(id_str) : 0;

    csilk_json_t* res = cashflow_schedule_get(pool, user_id, id);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        respond_not_found(c);
        return;
    }
    respond_ok(c, res);
}

void
cashflow_service_update_schedule(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id <= 0) {
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    const char*      id_str = csilk_get_param(c, "id");
    int64_t          id = id_str ? atoll(id_str) : 0;
    csilk_json_t*    body = csilk_bind_json(c);

    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    int64_t     source_asset_id = (int64_t)db_get_int(body, "source_asset_id");
    int64_t     target_asset_id = (int64_t)db_get_int(body, "target_asset_id");
    const char* name = csilk_json_get_string(body, "name");
    const char* flow_type = csilk_json_get_string(body, "flow_type");
    const char* frequency = csilk_json_get_string(body, "frequency");
    const char* start_date = csilk_json_get_string(body, "start_date");
    const char* end_date = csilk_json_get_string(body, "end_date");
    double      expected_amount = db_get_num(body, "expected_amount");
    const char* note = csilk_json_get_string(body, "note");

    int ret = cashflow_schedule_update(pool,
                                       user_id,
                                       id,
                                       source_asset_id,
                                       target_asset_id,
                                       name,
                                       flow_type,
                                       frequency,
                                       start_date,
                                       end_date,
                                       expected_amount,
                                       note);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to update cashflow schedule");
        return;
    }
    respond_ok(c, NULL);
}

void
cashflow_service_delete_schedule(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id <= 0) {
        return;
    }

    csilk_db_pool_t* pool = db_get_pool();
    const char*      id_str = csilk_get_param(c, "id");
    int64_t          id = id_str ? atoll(id_str) : 0;

    int ret = cashflow_schedule_delete(pool, user_id, id);
    if (ret != 0) {
        respond_error(c, 1002, "Failed to delete cashflow schedule");
        return;
    }
    respond_ok(c, NULL);
}

void
cashflow_service_get_calendar(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();

    const char* year_str = csilk_get_query(c, "year");
    const char* month_str = csilk_get_query(c, "month");

    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    int year = year_str ? atoi(year_str) : (tm_now.tm_year + 1900);
    int month = month_str ? atoi(month_str) : (tm_now.tm_mon + 1);
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

    /* 1. Query Actual Historical Transactions */
    csilk_json_t* actual_txs = cashflow_query_actual_transactions(pool, user_id, ym);
    if (actual_txs) {
        size_t count = csilk_json_array_size(actual_txs);
        for (size_t i = 0; i < count; ++i) {
            csilk_json_t* tx = csilk_json_array_get(actual_txs, i);
            int64_t       tx_id = (int64_t)db_get_int(tx, "id");
            double        amount = db_get_num(tx, "amount");
            const char*   tx_date = csilk_json_get_string(tx, "transaction_date");
            const char*   asset_name = csilk_json_get_string(tx, "asset_name");
            const char*   currency = csilk_json_get_string(tx, "asset_currency");
            const char*   note = csilk_json_get_string(tx, "note");

            char date_only[16] = {0};
            if (tx_date && strlen(tx_date) >= 10) {
                strncpy(date_only, tx_date, 10);
            }

            actual_total += amount;

            csilk_json_t* ev = csilk_json_object();
            csilk_json_add_number(ev, "id", (double)tx_id);
            csilk_json_add_string(ev, "date", date_only);
            csilk_json_add_string(
                ev, "name", note && note[0] ? note : (asset_name ? asset_name : "收入"));
            csilk_json_add_string(ev, "flow_type", "actual");
            csilk_json_add_number(ev, "amount", amount);
            csilk_json_add_string(ev, "currency", currency ? currency : "CNY");
            csilk_json_add_bool(ev, "is_actual", true);
            csilk_json_add_string(ev, "status", "confirmed");
            csilk_json_array_append(events_arr, ev);
        }
        csilk_json_free(actual_txs);
    }

    /* 2. Project Future Schedule Occurrences */
    csilk_json_t* schedules = cashflow_list_active_schedules(pool, user_id);
    double        annual_projected_total = 0.0;

    if (schedules) {
        size_t count = csilk_json_array_size(schedules);
        for (size_t i = 0; i < count; ++i) {
            csilk_json_t* sch = csilk_json_array_get(schedules, i);
            int64_t       sch_id = (int64_t)db_get_int(sch, "id");
            int64_t       src_asset_id = (int64_t)db_get_int(sch, "source_asset_id");
            int64_t       tgt_asset_id = (int64_t)db_get_int(sch, "target_asset_id");
            const char*   name = csilk_json_get_string(sch, "name");
            const char*   flow_type = csilk_json_get_string(sch, "flow_type");
            const char*   freq = csilk_json_get_string(sch, "frequency");
            const char*   start_date = csilk_json_get_string(sch, "start_date");
            const char*   end_date = csilk_json_get_string(sch, "end_date");
            const char*   src_name = csilk_json_get_string(sch, "source_asset_name");
            const char*   tgt_name = csilk_json_get_string(sch, "target_asset_name");
            const char*   currency = csilk_json_get_string(sch, "target_currency");
            double        expected_amount = db_get_num(sch, "expected_amount");

            if (!freq) {
                freq = "monthly";
            }
            if (!start_date) {
                start_date = "2026-01-01";
            }

            /* Calculate annual contribution */
            if (strcmp(freq, "monthly") == 0) {
                annual_projected_total += expected_amount * 12.0;
            } else if (strcmp(freq, "quarterly") == 0) {
                annual_projected_total += expected_amount * 4.0;
            } else if (strcmp(freq, "semi_annual") == 0) {
                annual_projected_total += expected_amount * 2.0;
            } else if (strcmp(freq, "annual") == 0 || strcmp(freq, "once") == 0) {
                annual_projected_total += expected_amount;
            }

            /* Parse start_date */
            int s_year = 2026, s_month = 1, s_day = 1;
            sscanf(start_date, "%d-%d-%d", &s_year, &s_month, &s_day);

            bool matches_this_month = false;
            int  target_day = s_day > 28 ? 28 : (s_day < 1 ? 1 : s_day);

            if (strcmp(freq, "once") == 0) {
                if (s_year == year && s_month == month) {
                    matches_this_month = true;
                }
            } else if (strcmp(freq, "monthly") == 0) {
                if (year > s_year || (year == s_year && month >= s_month)) {
                    matches_this_month = true;
                }
            } else if (strcmp(freq, "quarterly") == 0) {
                int total_months = (year - s_year) * 12 + (month - s_month);
                if (total_months >= 0 && (total_months % 3) == 0) {
                    matches_this_month = true;
                }
            } else if (strcmp(freq, "semi_annual") == 0) {
                int total_months = (year - s_year) * 12 + (month - s_month);
                if (total_months >= 0 && (total_months % 6) == 0) {
                    matches_this_month = true;
                }
            } else if (strcmp(freq, "annual") == 0) {
                if (year >= s_year && month == s_month) {
                    matches_this_month = true;
                }
            }

            if (matches_this_month) {
                char pdate[64];
                snprintf(pdate, sizeof(pdate), "%s-%02d", ym, target_day);

                /* Check end_date if set */
                if (end_date && end_date[0] && strcmp(pdate, end_date) > 0) {
                    matches_this_month = false;
                }

                if (matches_this_month) {
                    projected_total += expected_amount;

                    csilk_json_t* ev = csilk_json_object();
                    csilk_json_add_number(ev, "schedule_id", (double)sch_id);
                    csilk_json_add_number(ev, "source_asset_id", (double)src_asset_id);
                    csilk_json_add_number(ev, "target_asset_id", (double)tgt_asset_id);
                    csilk_json_add_string(ev, "date", pdate);
                    csilk_json_add_string(ev, "name", name ? name : "预期收益");
                    csilk_json_add_string(ev, "source_asset_name", src_name ? src_name : "");
                    csilk_json_add_string(ev, "target_asset_name", tgt_name ? tgt_name : "");
                    csilk_json_add_string(ev, "flow_type", flow_type ? flow_type : "dividend");
                    csilk_json_add_number(ev, "amount", expected_amount);
                    csilk_json_add_string(ev, "currency", currency ? currency : "CNY");
                    csilk_json_add_bool(ev, "is_actual", false);
                    csilk_json_add_string(ev, "status", "projected");
                    csilk_json_array_append(events_arr, ev);
                }
            }
        }
        csilk_json_free(schedules);
    }

    /* 3. Build Result Envelope */
    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "year", (double)year);
    csilk_json_add_number(res, "month", (double)month);
    csilk_json_add_string(res, "year_month", ym);
    csilk_json_add_number(res, "actual_total", actual_total);
    csilk_json_add_number(res, "projected_total", projected_total);
    csilk_json_add_number(res, "annual_projected_total", annual_projected_total);
    csilk_json_add_array(res, "events", events_arr);

    respond_ok(c, res);
}

void
cashflow_service_confirm(csilk_ctx_t* c)
{
    int64_t          user_id = ctx_user_id(c);
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t*    body = csilk_bind_json(c);

    if (!body) {
        respond_bad_request(c, "Invalid JSON body");
        return;
    }

    int64_t     target_asset_id = (int64_t)db_get_int(body, "target_asset_id");
    int64_t     source_asset_id = (int64_t)db_get_int(body, "source_asset_id");
    double      amount = db_get_num(body, "amount");
    const char* date = csilk_json_get_string(body, "date");
    const char* name = csilk_json_get_string(body, "name");
    const char* note = csilk_json_get_string(body, "note");

    if (target_asset_id <= 0 || amount <= 0.0 || !date || !date[0]) {
        respond_bad_request(c, "Missing required fields (target_asset_id, amount, date)");
        return;
    }

    char tx_note[256];
    snprintf(tx_note, sizeof(tx_note), "%s %s", name ? name : "现金流收益入账", note ? note : "");

    char full_date[32];
    snprintf(full_date, sizeof(full_date), "%s 10:00:00", date);

    csilk_db_exec(pool, "BEGIN TRANSACTION");

    int64_t tx_id = tx_insert(pool,
                              user_id,
                              target_asset_id,
                              source_asset_id,
                              0, /* category_id */
                              "income",
                              "income",
                              "in",
                              "out",
                              amount,
                              0.0,
                              0.0,
                              0.0,
                              "CNY",
                              full_date,
                              tx_note);
    if (tx_id <= 0) {
        csilk_db_exec(pool, "ROLLBACK");
        respond_error(c, 1002, "Failed to create transaction for cash flow income");
        return;
    }

    if (balance_apply_delta(
            pool, target_asset_id, user_id, amount, "transaction", tx_id, tx_note) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        respond_error(c, 1002, "现金流余额更新失败");
        return;
    }

    csilk_db_exec(pool, "COMMIT");

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "transaction_id", (double)tx_id);
    csilk_json_add_number(res, "amount", amount);
    respond_ok(c, res);
}
