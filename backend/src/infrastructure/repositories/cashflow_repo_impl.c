#include "infrastructure/repositories/cashflow_repo_impl.h"
#include "infrastructure/repositories/account_permission_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static csilk_json_t*
query_cashflow_schedules(
    void* pool, int64_t user_id, int64_t ledger_id, int64_t id, bool only_active)
{
    char uid[32], lid[32], sid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(sid, sizeof(sid), "%lld", (long long)id);

    const char* scope_where =
        (ledger_id > 0) ? MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("s", "ta") : "s.user_id = ?";
    const char* active_cond = only_active ? " AND s.status = 'active'" : "";
    const char* id_cond = (id > 0) ? " AND s.id = ?" : "";
    const char* order_by = (id > 0) ? "" : " ORDER BY s.id DESC";

    char sql[2560];
    snprintf(
        sql,
        sizeof(sql),
        "SELECT s.id, s.user_id, COALESCE(NULLIF(s.ledger_id, 0), ta.ledger_id) AS ledger_id, "
        "       s.source_asset_id, s.target_asset_id, s.name, "
        "       s.flow_type, s.frequency, s.start_date, s.end_date, s.expected_amount, "
        "       s.status, s.note, CAST(s.created_at AS TEXT) AS created_at, "
        "       CAST(s.updated_at AS TEXT) AS updated_at, "
        "       sa.name AS source_asset_name, sa.symbol AS source_symbol, "
        "       ta.name AS target_asset_name, ta.currency AS target_currency "
        "FROM cashflow_schedules s "
        "JOIN assets sa ON sa.id = s.source_asset_id "
        "JOIN assets ta ON ta.id = s.target_asset_id "
        "WHERE %s AND " MF_RBAC_ASSET_VISIBLE_SQL("sa") " AND " MF_RBAC_ASSET_VISIBLE_SQL("ta")
            "%s%s%s",
        scope_where,
        active_cond,
        id_cond,
        order_by);

    const char* params[16];
    int         pidx = 0;
    if (ledger_id > 0) {
        params[pidx++] = lid;
        params[pidx++] = uid;
        params[pidx++] = uid;
        params[pidx++] = uid;
    } else {
        params[pidx++] = uid;
    }
    params[pidx++] = uid;
    params[pidx++] = uid;
    params[pidx++] = uid;
    params[pidx++] = uid;
    if (id > 0) {
        params[pidx++] = sid;
    }
    params[pidx] = NULL;

    return csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
}

static const char* SQL_SCHEDULE_CREATE =
    "INSERT INTO cashflow_schedules (user_id, ledger_id, source_asset_id, target_asset_id, name, "
    "                                flow_type, frequency, start_date, end_date, "
    "                                expected_amount, status, note) "
    "VALUES (?, COALESCE(NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), "
    "        (SELECT ledger_id FROM assets WHERE id = ?)), ?, ?, ?, ?, ?, ?, ?, ?, 'active', ?) "
    "RETURNING id";

/* --- Helper: parse a schedule row into mf_cashflow_schedule_t --- */
static void
parse_schedule_row(csilk_json_t* row, int64_t user_id, mf_cashflow_schedule_t* out)
{
    out->id = (int64_t)db_get_int(row, "id");
    int64_t row_uid = (int64_t)db_get_int(row, "user_id");
    out->user_id = row_uid > 0 ? row_uid : user_id;
    out->ledger_id = (int64_t)db_get_int(row, "ledger_id");
    out->source_asset_id = (int64_t)db_get_int(row, "source_asset_id");
    out->target_asset_id = (int64_t)db_get_int(row, "target_asset_id");
    const char* s = csilk_json_get_string(row, "name");
    if (s) {
        snprintf(out->name, sizeof(out->name), "%s", s);
    }
    s = csilk_json_get_string(row, "flow_type");
    if (s) {
        snprintf(out->flow_type, sizeof(out->flow_type), "%s", s);
    }
    s = csilk_json_get_string(row, "frequency");
    if (s) {
        snprintf(out->frequency, sizeof(out->frequency), "%s", s);
    }
    s = csilk_json_get_string(row, "start_date");
    if (s) {
        snprintf(out->start_date, sizeof(out->start_date), "%s", s);
    }
    s = csilk_json_get_string(row, "end_date");
    if (s) {
        snprintf(out->end_date, sizeof(out->end_date), "%s", s);
    }
    s = csilk_json_get_string(row, "note");
    if (s) {
        snprintf(out->note, sizeof(out->note), "%s", s);
    }
    s = csilk_json_get_string(row, "status");
    if (s) {
        snprintf(out->status, sizeof(out->status), "%s", s);
    }
    const char* cur = csilk_json_get_string(row, "target_currency");
    currency_t  c = currency_from_str(cur ? cur : "CNY");
    out->expected_amount = db_get_money(row, "expected_amount", c);
}

/* --- Helper: convert schedule list to mf_cashflow_schedule_t array --- */
static int
convert_schedule_list(csilk_json_t*            json,
                      int64_t                  user_id,
                      mf_cashflow_schedule_t** out_list,
                      size_t*                  out_count)
{
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
        parse_schedule_row(row, user_id, &arr[i]);
    }
    csilk_json_free(json);
    *out_list = arr;
    *out_count = n;
    return 0;
}

int
mf_cashflow_repo_list(void*                    pool,
                      int64_t                  user_id,
                      int64_t                  ledger_id,
                      mf_cashflow_schedule_t** out_list,
                      size_t*                  out_count)
{
    if (!pool || !out_list || !out_count) {
        return -1;
    }
    *out_list = NULL;
    *out_count = 0;
    csilk_json_t* json = query_cashflow_schedules(pool, user_id, ledger_id, 0, false);
    if (!json) {
        return -1;
    }
    return convert_schedule_list(json, user_id, out_list, out_count);
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
mf_cashflow_repo_get(void*                   pool,
                     int64_t                 user_id,
                     int64_t                 ledger_id,
                     int64_t                 id,
                     mf_cashflow_schedule_t* out_schedule)
{
    if (!pool || !out_schedule || id <= 0) {
        return -1;
    }
    memset(out_schedule, 0, sizeof(*out_schedule));
    csilk_json_t* res = query_cashflow_schedules(pool, user_id, ledger_id, id, false);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }
    parse_schedule_row(csilk_json_array_get(res, 0), user_id, out_schedule);
    csilk_json_free(res);
    return 0;
}

int
mf_cashflow_repo_create(void* pool, const mf_cashflow_schedule_t* s, int64_t* out_id)
{
    if (!pool || !s) {
        return -1;
    }
    char uid[32], lid[32], said[32], taid[32], eamt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)s->user_id);
    if (s->ledger_id > 0) {
        snprintf(lid, sizeof(lid), "%lld", (long long)s->ledger_id);
    } else {
        lid[0] = '\0';
    }
    snprintf(said, sizeof(said), "%lld", (long long)s->source_asset_id);
    snprintf(taid, sizeof(taid), "%lld", (long long)s->target_asset_id);
    snprintf(eamt, sizeof(eamt), "%.4f", money_to_double(s->expected_amount));
    const char*   params[] = {uid,
                              lid,
                              taid,
                              said,
                              taid,
                              s->name[0] ? s->name : "",
                              s->flow_type[0] ? s->flow_type : "dividend",
                              s->frequency[0] ? s->frequency : "monthly",
                              s->start_date[0] ? s->start_date : "",
                              s->end_date[0] ? s->end_date : "",
                              eamt,
                              s->note[0] ? s->note : "",
                              NULL};
    csilk_json_t* res =
        csilk_db_query_param_json((csilk_db_pool_t*)pool, SQL_SCHEDULE_CREATE, params);
    int64_t id = -1;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
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
    char uid[32], lid[32], sid[32], said[32], taid[32], eamt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)s->user_id);
    if (s->ledger_id > 0) {
        snprintf(lid, sizeof(lid), "%lld", (long long)s->ledger_id);
    } else {
        lid[0] = '\0';
    }
    snprintf(sid, sizeof(sid), "%lld", (long long)s->id);
    snprintf(said, sizeof(said), "%lld", (long long)s->source_asset_id);
    snprintf(taid, sizeof(taid), "%lld", (long long)s->target_asset_id);
    snprintf(eamt, sizeof(eamt), "%.4f", money_to_double(s->expected_amount));

    char        sql[2048];
    const char* params[24];
    int         pidx = 0;
    params[pidx++] = said;
    params[pidx++] = taid;
    params[pidx++] = s->name[0] ? s->name : "";
    params[pidx++] = s->flow_type[0] ? s->flow_type : "dividend";
    params[pidx++] = s->frequency[0] ? s->frequency : "monthly";
    params[pidx++] = s->start_date[0] ? s->start_date : "";
    params[pidx++] = s->end_date[0] ? s->end_date : "";
    params[pidx++] = eamt;
    params[pidx++] = s->note[0] ? s->note : "";
    params[pidx++] = lid;
    params[pidx++] = taid;

    if (s->ledger_id > 0) {
        snprintf(sql,
                 sizeof(sql),
                 "UPDATE cashflow_schedules "
                 "SET source_asset_id = ?, target_asset_id = ?, name = ?, flow_type = ?, "
                 "    frequency = ?, start_date = ?, end_date = ?, expected_amount = ?, "
                 "    note = ?, ledger_id = COALESCE(NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), "
                 "    (SELECT ledger_id FROM assets WHERE id = ?)), "
                 "    updated_at = CURRENT_TIMESTAMP "
                 "WHERE id = ? AND COALESCE(NULLIF(cashflow_schedules.ledger_id, 0), "
                 "    (SELECT ledger_id FROM assets WHERE id = cashflow_schedules.target_asset_id)) "
                 "    = ? AND " MF_LEDGER_SHARED_WRITE_FALLBACK_SQL("cashflow_schedules",
                                                                   "target_asset_id") " "
                 "RETURNING id");
        params[pidx++] = sid;
        params[pidx++] = lid;
        params[pidx++] = uid;
        params[pidx++] = uid;
        params[pidx++] = uid;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "UPDATE cashflow_schedules "
                 "SET source_asset_id = ?, target_asset_id = ?, name = ?, flow_type = ?, "
                 "    frequency = ?, start_date = ?, end_date = ?, expected_amount = ?, "
                 "    note = ?, ledger_id = COALESCE(NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), "
                 "    (SELECT ledger_id FROM assets WHERE id = ?)), "
                 "    updated_at = CURRENT_TIMESTAMP "
                 "WHERE user_id = ? AND id = ? RETURNING id");
        params[pidx++] = uid;
        params[pidx++] = sid;
    }
    params[pidx] = NULL;

    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
mf_cashflow_repo_delete(void* pool, int64_t user_id, int64_t ledger_id, int64_t id)
{
    if (!pool || id <= 0) {
        return -1;
    }
    char uid[32], lid[32], sid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(sid, sizeof(sid), "%lld", (long long)id);

    char        sql[1536];
    const char* params[10];
    int         pidx = 0;
    if (ledger_id > 0) {
        snprintf(
            sql,
            sizeof(sql),
            "DELETE FROM cashflow_schedules WHERE id = ? "
            "AND COALESCE(NULLIF(cashflow_schedules.ledger_id, 0), "
            "(SELECT ledger_id FROM assets WHERE id = cashflow_schedules.target_asset_id)) = ? "
            "AND " MF_LEDGER_SHARED_WRITE_FALLBACK_SQL("cashflow_schedules", "target_asset_id") " "
            "RETURNING id");
        params[pidx++] = sid;
        params[pidx++] = lid;
        params[pidx++] = uid;
        params[pidx++] = uid;
        params[pidx++] = uid;
    } else {
        snprintf(sql,
                 sizeof(sql),
                 "DELETE FROM cashflow_schedules WHERE user_id = ? AND id = ? RETURNING id");
        params[pidx++] = uid;
        params[pidx++] = sid;
    }
    params[pidx] = NULL;

    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
mf_cashflow_repo_list_active(void*                    pool,
                             int64_t                  user_id,
                             int64_t                  ledger_id,
                             mf_cashflow_schedule_t** out_list,
                             size_t*                  out_count)
{
    if (!pool || !out_list || !out_count) {
        return -1;
    }
    *out_list = NULL;
    *out_count = 0;
    csilk_json_t* json = query_cashflow_schedules(pool, user_id, ledger_id, 0, true);
    if (!json) {
        return -1;
    }
    return convert_schedule_list(json, user_id, out_list, out_count);
}

int
mf_cashflow_repo_get_actual_events(void*                 pool,
                                   int64_t               user_id,
                                   int64_t               ledger_id,
                                   const char*           year_month,
                                   mf_cashflow_event_t** out_events,
                                   size_t*               out_count)
{
    if (!pool || !out_events || !out_count) {
        return -1;
    }
    *out_events = NULL;
    *out_count = 0;
    char uid[32], lid[32], ym_prefix[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
    snprintf(ym_prefix, sizeof(ym_prefix), "%s%%", year_month ? year_month : "");

    const char* scope_where =
        (ledger_id > 0) ? MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("t", "a") : "t.user_id = ?";
    char sql[2048];
    snprintf(sql,
             sizeof(sql),
             "SELECT t.id, t.user_id, t.asset_id, t.amount, t.transaction_type, "
             "CAST(t.transaction_date AS TEXT) AS transaction_date, t.note, "
             "       a.name AS asset_name, a.currency AS asset_currency "
             "FROM transactions t "
             "JOIN assets a ON a.id = t.asset_id "
             "WHERE %s "
             "  AND t.transaction_type IN ('income', 'deposit', 'dividend', 'interest') "
             "  AND CAST(t.transaction_date AS TEXT) LIKE ? "
             "  AND " MF_RBAC_ASSET_UNMASKED_SQL("a") " "
             "ORDER BY t.transaction_date ASC, t.id ASC",
             scope_where);

    const char* params[12];
    int         pidx = 0;
    if (ledger_id > 0) {
        params[pidx++] = lid;
        params[pidx++] = uid;
        params[pidx++] = uid;
        params[pidx++] = uid;
    } else {
        params[pidx++] = uid;
    }
    params[pidx++] = ym_prefix;
    params[pidx++] = uid;
    params[pidx++] = uid;
    params[pidx] = NULL;

    csilk_json_t* actual_txs = csilk_db_query_param_json((csilk_db_pool_t*)pool, sql, params);
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

csilk_json_t*
cashflow_repo_list_detailed(void* pool, int64_t user_id, int64_t ledger_id)
{
    if (!pool || user_id <= 0) {
        return NULL;
    }
    return query_cashflow_schedules(pool, user_id, ledger_id, 0, false);
}

csilk_json_t*
cashflow_repo_get_detailed(void* pool, int64_t user_id, int64_t ledger_id, int64_t id)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return NULL;
    }
    return query_cashflow_schedules(pool, user_id, ledger_id, id, false);
}
