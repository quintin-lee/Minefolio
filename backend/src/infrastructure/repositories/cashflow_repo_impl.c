#include "infrastructure/repositories/cashflow_repo_impl.h"
#include "repositories/cashflow_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
mf_cashflow_repo_list(void*                    pool,
                      int64_t                  user_id,
                      mf_cashflow_schedule_t** out_list,
                      size_t*                  out_count)
{
    if (!pool || !out_list || !out_count) {
        return -1;
    }
    *out_list = NULL;
    *out_count = 0;

    csilk_json_t* json = cashflow_schedule_list((csilk_db_pool_t*)pool, user_id);
    if (!json) {
        return -1;
    }

    size_t n = csilk_json_array_size(json);
    if (n == 0) {
        csilk_json_free(json);
        return 0;
    }

    mf_cashflow_schedule_t* arr =
        (mf_cashflow_schedule_t*)calloc(n, sizeof(mf_cashflow_schedule_t));
    if (!arr) {
        csilk_json_free(json);
        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(json, i);
        arr[i].id = (int64_t)db_get_int(row, "id");
        arr[i].user_id = user_id;
        arr[i].source_asset_id = (int64_t)db_get_int(row, "source_asset_id");
        arr[i].target_asset_id = (int64_t)db_get_int(row, "target_asset_id");
        const char* s = csilk_json_get_string(row, "name");
        if (s) {
            snprintf(arr[i].name, sizeof(arr[i].name), "%s", s);
        }
        s = csilk_json_get_string(row, "flow_type");
        if (s) {
            snprintf(arr[i].flow_type, sizeof(arr[i].flow_type), "%s", s);
        }
        s = csilk_json_get_string(row, "frequency");
        if (s) {
            snprintf(arr[i].frequency, sizeof(arr[i].frequency), "%s", s);
        }
        s = csilk_json_get_string(row, "start_date");
        if (s) {
            snprintf(arr[i].start_date, sizeof(arr[i].start_date), "%s", s);
        }
        s = csilk_json_get_string(row, "end_date");
        if (s) {
            snprintf(arr[i].end_date, sizeof(arr[i].end_date), "%s", s);
        }
        s = csilk_json_get_string(row, "note");
        if (s) {
            snprintf(arr[i].note, sizeof(arr[i].note), "%s", s);
        }
        s = csilk_json_get_string(row, "status");
        if (s) {
            snprintf(arr[i].status, sizeof(arr[i].status), "%s", s);
        }

        const char* cur = csilk_json_get_string(row, "target_currency");
        currency_t  c = currency_from_str(cur ? cur : "CNY");
        arr[i].expected_amount = db_get_money(row, "expected_amount", c);
    }

    csilk_json_free(json);
    *out_list = arr;
    *out_count = n;
    return 0;
}

void
mf_cashflow_repo_free_list(mf_cashflow_schedule_t* list, size_t count)
{
    (void)count;
    if (list) {
        free(list);
    }
}

int
mf_cashflow_repo_get(void* pool, int64_t user_id, int64_t id, mf_cashflow_schedule_t* out_schedule)
{
    if (!pool || !out_schedule || id <= 0) {
        return -1;
    }
    memset(out_schedule, 0, sizeof(*out_schedule));

    csilk_json_t* res = cashflow_schedule_get((csilk_db_pool_t*)pool, user_id, id);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }

    csilk_json_t* row = csilk_json_array_get(res, 0);
    out_schedule->id = (int64_t)db_get_int(row, "id");
    out_schedule->user_id = user_id;
    out_schedule->source_asset_id = (int64_t)db_get_int(row, "source_asset_id");
    out_schedule->target_asset_id = (int64_t)db_get_int(row, "target_asset_id");
    const char* s = csilk_json_get_string(row, "name");
    if (s) {
        snprintf(out_schedule->name, sizeof(out_schedule->name), "%s", s);
    }
    s = csilk_json_get_string(row, "flow_type");
    if (s) {
        snprintf(out_schedule->flow_type, sizeof(out_schedule->flow_type), "%s", s);
    }
    s = csilk_json_get_string(row, "frequency");
    if (s) {
        snprintf(out_schedule->frequency, sizeof(out_schedule->frequency), "%s", s);
    }
    s = csilk_json_get_string(row, "start_date");
    if (s) {
        snprintf(out_schedule->start_date, sizeof(out_schedule->start_date), "%s", s);
    }
    s = csilk_json_get_string(row, "end_date");
    if (s) {
        snprintf(out_schedule->end_date, sizeof(out_schedule->end_date), "%s", s);
    }
    s = csilk_json_get_string(row, "note");
    if (s) {
        snprintf(out_schedule->note, sizeof(out_schedule->note), "%s", s);
    }
    s = csilk_json_get_string(row, "status");
    if (s) {
        snprintf(out_schedule->status, sizeof(out_schedule->status), "%s", s);
    }

    const char* cur = csilk_json_get_string(row, "target_currency");
    currency_t  c = currency_from_str(cur ? cur : "CNY");
    out_schedule->expected_amount = db_get_money(row, "expected_amount", c);

    csilk_json_free(res);
    return 0;
}

int
mf_cashflow_repo_create(void* pool, const mf_cashflow_schedule_t* s, int64_t* out_id)
{
    if (!pool || !s) {
        return -1;
    }
    int64_t id = cashflow_schedule_create((csilk_db_pool_t*)pool,
                                          s->user_id,
                                          s->source_asset_id,
                                          s->target_asset_id,
                                          s->name,
                                          s->flow_type,
                                          s->frequency,
                                          s->start_date,
                                          s->end_date,
                                          money_to_double(s->expected_amount),
                                          s->note);
    if (id <= 0) {
        return -1;
    }
    if (out_id) {
        *out_id = id;
    }
    return 0;
}

int
mf_cashflow_repo_update(void* pool, const mf_cashflow_schedule_t* s)
{
    if (!pool || !s || s->id <= 0) {
        return -1;
    }
    return cashflow_schedule_update((csilk_db_pool_t*)pool,
                                    s->user_id,
                                    s->id,
                                    s->source_asset_id,
                                    s->target_asset_id,
                                    s->name,
                                    s->flow_type,
                                    s->frequency,
                                    s->start_date,
                                    s->end_date,
                                    money_to_double(s->expected_amount),
                                    s->note);
}

int
mf_cashflow_repo_delete(void* pool, int64_t user_id, int64_t id)
{
    if (!pool || id <= 0) {
        return -1;
    }
    return cashflow_schedule_delete((csilk_db_pool_t*)pool, user_id, id);
}

int
mf_cashflow_repo_list_active(void*                    pool,
                             int64_t                  user_id,
                             mf_cashflow_schedule_t** out_list,
                             size_t*                  out_count)
{
    if (!pool || !out_list || !out_count) {
        return -1;
    }
    *out_list = NULL;
    *out_count = 0;

    csilk_json_t* json = cashflow_list_active_schedules((csilk_db_pool_t*)pool, user_id);
    if (!json) {
        return -1;
    }

    size_t n = csilk_json_array_size(json);
    if (n == 0) {
        csilk_json_free(json);
        return 0;
    }

    mf_cashflow_schedule_t* arr =
        (mf_cashflow_schedule_t*)calloc(n, sizeof(mf_cashflow_schedule_t));
    if (!arr) {
        csilk_json_free(json);
        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        csilk_json_t* row = csilk_json_array_get(json, i);
        arr[i].id = (int64_t)db_get_int(row, "id");
        arr[i].user_id = user_id;
        arr[i].source_asset_id = (int64_t)db_get_int(row, "source_asset_id");
        arr[i].target_asset_id = (int64_t)db_get_int(row, "target_asset_id");
        const char* s = csilk_json_get_string(row, "name");
        if (s) {
            snprintf(arr[i].name, sizeof(arr[i].name), "%s", s);
        }
        s = csilk_json_get_string(row, "flow_type");
        if (s) {
            snprintf(arr[i].flow_type, sizeof(arr[i].flow_type), "%s", s);
        }
        s = csilk_json_get_string(row, "frequency");
        if (s) {
            snprintf(arr[i].frequency, sizeof(arr[i].frequency), "%s", s);
        }
        s = csilk_json_get_string(row, "start_date");
        if (s) {
            snprintf(arr[i].start_date, sizeof(arr[i].start_date), "%s", s);
        }
        s = csilk_json_get_string(row, "end_date");
        if (s) {
            snprintf(arr[i].end_date, sizeof(arr[i].end_date), "%s", s);
        }

        const char* cur = csilk_json_get_string(row, "target_currency");
        currency_t  c = currency_from_str(cur ? cur : "CNY");
        arr[i].expected_amount = db_get_money(row, "expected_amount", c);
    }

    csilk_json_free(json);
    *out_list = arr;
    *out_count = n;
    return 0;
}

int
mf_cashflow_repo_get_actual_events(void*                 pool,
                                   int64_t               user_id,
                                   const char*           year_month,
                                   mf_cashflow_event_t** out_events,
                                   size_t*               out_count)
{
    if (!pool || !out_events || !out_count) {
        return -1;
    }
    *out_events = NULL;
    *out_count = 0;

    csilk_json_t* actual_txs =
        cashflow_query_actual_transactions((csilk_db_pool_t*)pool, user_id, year_month);
    if (!actual_txs) {
        return 0;
    }

    size_t count = csilk_json_array_size(actual_txs);
    if (count == 0) {
        csilk_json_free(actual_txs);
        return 0;
    }

    mf_cashflow_event_t* evs = (mf_cashflow_event_t*)calloc(count, sizeof(mf_cashflow_event_t));
    if (!evs) {
        csilk_json_free(actual_txs);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        csilk_json_t* tx = csilk_json_array_get(actual_txs, i);
        evs[i].id = (int64_t)db_get_int(tx, "id");
        const char* tx_date = csilk_json_get_string(tx, "transaction_date");
        if (tx_date && strlen(tx_date) >= 10) {
            strncpy(evs[i].date, tx_date, 10);
        }
        const char* note = csilk_json_get_string(tx, "note");
        const char* asset_name = csilk_json_get_string(tx, "asset_name");
        snprintf(evs[i].name,
                 sizeof(evs[i].name),
                 "%s",
                 (note && note[0]) ? note : (asset_name ? asset_name : "收入"));
        snprintf(evs[i].flow_type, sizeof(evs[i].flow_type), "actual");
        const char* cur = csilk_json_get_string(tx, "asset_currency");
        evs[i].currency = currency_from_str(cur ? cur : "CNY");
        evs[i].amount = db_get_money(tx, "amount", evs[i].currency);
        evs[i].is_actual = true;
        snprintf(evs[i].status, sizeof(evs[i].status), "confirmed");
    }

    csilk_json_free(actual_txs);
    *out_events = evs;
    *out_count = count;
    return 0;
}

void
mf_cashflow_repo_free_events(mf_cashflow_event_t* events, size_t count)
{
    (void)count;
    if (events) {
        free(events);
    }
}
