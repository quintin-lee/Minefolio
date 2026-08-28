#include "repositories/cashflow_repo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

csilk_json_t*
cashflow_schedule_list(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql =
        "SELECT s.id, s.user_id, s.source_asset_id, s.target_asset_id, s.name, "
        "       s.flow_type, s.frequency, s.start_date, s.end_date, s.expected_amount, "
        "       s.status, s.note, CAST(s.created_at AS TEXT) AS created_at, CAST(s.updated_at AS "
        "TEXT) AS updated_at, "
        "       sa.name AS source_asset_name, sa.symbol AS source_symbol, "
        "       ta.name AS target_asset_name, ta.currency AS target_currency "
        "FROM cashflow_schedules s "
        "JOIN assets sa ON sa.id = s.source_asset_id "
        "JOIN assets ta ON ta.id = s.target_asset_id "
        "WHERE s.user_id = ? "
        "ORDER BY s.id DESC";

    const char* params[] = {uid, NULL};
    return csilk_db_query_param_json(pool, sql, params);
}

csilk_json_t*
cashflow_schedule_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], sid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(sid, sizeof(sid), "%lld", (long long)id);

    const char* sql =
        "SELECT s.id, s.user_id, s.source_asset_id, s.target_asset_id, s.name, "
        "       s.flow_type, s.frequency, s.start_date, s.end_date, s.expected_amount, "
        "       s.status, s.note, CAST(s.created_at AS TEXT) AS created_at, CAST(s.updated_at AS "
        "TEXT) AS updated_at, "
        "       sa.name AS source_asset_name, sa.symbol AS source_symbol, "
        "       ta.name AS target_asset_name, ta.currency AS target_currency "
        "FROM cashflow_schedules s "
        "JOIN assets sa ON sa.id = s.source_asset_id "
        "JOIN assets ta ON ta.id = s.target_asset_id "
        "WHERE s.user_id = ? AND s.id = ?";

    const char* params[] = {uid, sid, NULL};
    return csilk_db_query_param_json(pool, sql, params);
}

int64_t
cashflow_schedule_create(csilk_db_pool_t* pool,
                         int64_t          user_id,
                         int64_t          source_asset_id,
                         int64_t          target_asset_id,
                         const char*      name,
                         const char*      flow_type,
                         const char*      frequency,
                         const char*      start_date,
                         const char*      end_date,
                         double           expected_amount,
                         const char*      note)
{
    char uid[32], said[32], taid[32], eamt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(said, sizeof(said), "%lld", (long long)source_asset_id);
    snprintf(taid, sizeof(taid), "%lld", (long long)target_asset_id);
    snprintf(eamt, sizeof(eamt), "%.4f", expected_amount);

    const char* sql =
        "INSERT INTO cashflow_schedules (user_id, source_asset_id, target_asset_id, name, "
        "                                flow_type, frequency, start_date, end_date, "
        "                                expected_amount, status, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'active', ?) RETURNING id";

    const char* params[] = {uid,
                            said,
                            taid,
                            name ? name : "",
                            flow_type ? flow_type : "dividend",
                            frequency ? frequency : "monthly",
                            start_date ? start_date : "",
                            end_date ? end_date : "",
                            eamt,
                            note ? note : "",
                            NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int64_t       id = -1;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

int
cashflow_schedule_update(csilk_db_pool_t* pool,
                         int64_t          user_id,
                         int64_t          id,
                         int64_t          source_asset_id,
                         int64_t          target_asset_id,
                         const char*      name,
                         const char*      flow_type,
                         const char*      frequency,
                         const char*      start_date,
                         const char*      end_date,
                         double           expected_amount,
                         const char*      note)
{
    char uid[32], sid[32], said[32], taid[32], eamt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(sid, sizeof(sid), "%lld", (long long)id);
    snprintf(said, sizeof(said), "%lld", (long long)source_asset_id);
    snprintf(taid, sizeof(taid), "%lld", (long long)target_asset_id);
    snprintf(eamt, sizeof(eamt), "%.4f", expected_amount);

    const char* sql = "UPDATE cashflow_schedules "
                      "SET source_asset_id = ?, target_asset_id = ?, name = ?, flow_type = ?, "
                      "    frequency = ?, start_date = ?, end_date = ?, expected_amount = ?, "
                      "    note = ?, updated_at = CURRENT_TIMESTAMP "
                      "WHERE user_id = ? AND id = ? RETURNING id";

    const char* params[] = {said,
                            taid,
                            name ? name : "",
                            flow_type ? flow_type : "dividend",
                            frequency ? frequency : "monthly",
                            start_date ? start_date : "",
                            end_date ? end_date : "",
                            eamt,
                            note ? note : "",
                            uid,
                            sid,
                            NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
cashflow_schedule_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], sid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(sid, sizeof(sid), "%lld", (long long)id);

    const char* sql = "DELETE FROM cashflow_schedules WHERE user_id = ? AND id = ? RETURNING id";
    const char* params[] = {uid, sid, NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

csilk_json_t*
cashflow_list_active_schedules(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql =
        "SELECT s.id, s.user_id, s.source_asset_id, s.target_asset_id, s.name, "
        "       s.flow_type, s.frequency, s.start_date, s.end_date, s.expected_amount, "
        "       s.status, s.note, "
        "       sa.name AS source_asset_name, ta.name AS target_asset_name, ta.currency AS "
        "target_currency "
        "FROM cashflow_schedules s "
        "JOIN assets sa ON sa.id = s.source_asset_id "
        "JOIN assets ta ON ta.id = s.target_asset_id "
        "WHERE s.user_id = ? AND s.status = 'active'";

    const char* params[] = {uid, NULL};
    return csilk_db_query_param_json(pool, sql, params);
}

csilk_json_t*
cashflow_query_actual_transactions(csilk_db_pool_t* pool, int64_t user_id, const char* year_month)
{
    char uid[32], ym_prefix[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(ym_prefix, sizeof(ym_prefix), "%s%%", year_month ? year_month : "");

    const char* sql = "SELECT t.id, t.user_id, t.asset_id, t.amount, t.transaction_type, "
                      "CAST(t.transaction_date AS TEXT) AS transaction_date, t.note, "
                      "       a.name AS asset_name, a.currency AS asset_currency "
                      "FROM transactions t "
                      "JOIN assets a ON a.id = t.asset_id "
                      "WHERE t.user_id = ? "
                      "  AND t.transaction_type IN ('income', 'deposit') "
                      "  AND t.transaction_date LIKE ? "
                      "ORDER BY t.transaction_date ASC, t.id ASC";

    const char* params[] = {uid, ym_prefix, NULL};
    return csilk_db_query_param_json(pool, sql, params);
}
