/** @file dca_repo_impl.c @brief DCA plan repository SQL implementation */

#include "infrastructure/repositories/dca_repo_impl.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

csilk_json_t*
mf_dca_repo_plan_list(void* db_pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    const char* sql =
        "SELECT p.id, p.user_id, p.target_asset_id, p.funding_asset_id, p.name, "
        "       p.frequency, p.day_of_period, p.amount, p.target_profit_rate, "
        "       p.target_total_amount, p.target_total_periods, p.status, p.note, "
        "       CAST(p.created_at AS TEXT) AS created_at, CAST(p.updated_at AS TEXT) AS "
        "updated_at, "
        "       ta.name AS target_asset_name, ta.symbol AS target_symbol, "
        "       ta.net_value AS target_net_value, ta.quantity AS target_quantity, "
        "       ta.cost_basis AS target_cost_basis, ta.current_value AS target_current_value, "
        "       ta.quote_source AS target_quote_source, ta.currency AS target_currency, "
        "       fa.name AS funding_asset_name, fa.currency AS funding_currency, "
        "       (SELECT COUNT(*) FROM dca_executions e WHERE e.plan_id = p.id AND e.status = "
        "'confirmed') AS executed_periods, "
        "       (SELECT COALESCE(SUM(actual_amount), 0) FROM dca_executions e WHERE e.plan_id = "
        "p.id AND e.status = 'confirmed') AS total_invested_amount "
        "FROM dca_plans p "
        "JOIN assets ta ON ta.id = p.target_asset_id "
        "JOIN assets fa ON fa.id = p.funding_asset_id "
        "WHERE p.user_id = ? "
        "ORDER BY p.id DESC";
    return csilk_db_query_param_json((csilk_db_pool_t*)db_pool, sql, (const char*[]){uid, NULL});
}

csilk_json_t*
mf_dca_repo_plan_get(void* db_pool, int64_t user_id, int64_t id)
{
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    const char* sql =
        "SELECT p.id, p.user_id, p.target_asset_id, p.funding_asset_id, p.name, "
        "       p.frequency, p.day_of_period, p.amount, p.target_profit_rate, "
        "       p.target_total_amount, p.target_total_periods, p.status, p.note, "
        "       CAST(p.created_at AS TEXT) AS created_at, CAST(p.updated_at AS TEXT) AS "
        "updated_at, "
        "       ta.name AS target_asset_name, ta.symbol AS target_symbol, "
        "       ta.net_value AS target_net_value, ta.quantity AS target_quantity, "
        "       ta.cost_basis AS target_cost_basis, ta.current_value AS target_current_value, "
        "       ta.quote_source AS target_quote_source, ta.currency AS target_currency, "
        "       fa.name AS funding_asset_name, fa.currency AS funding_currency, "
        "       (SELECT COUNT(*) FROM dca_executions e WHERE e.plan_id = p.id AND e.status = "
        "'confirmed') AS executed_periods, "
        "       (SELECT COALESCE(SUM(actual_amount), 0) FROM dca_executions e WHERE e.plan_id = "
        "p.id AND e.status = 'confirmed') AS total_invested_amount "
        "FROM dca_plans p "
        "JOIN assets ta ON ta.id = p.target_asset_id "
        "JOIN assets fa ON fa.id = p.funding_asset_id "
        "WHERE p.user_id = ? AND p.id = ?";
    csilk_json_t* arr = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool, sql, (const char*[]){uid, idstr, NULL});
    if (!arr || csilk_json_array_size(arr) == 0) {
        if (arr) {
            csilk_json_free(arr);
        }
        return NULL;
    }
    return arr;
}

int64_t
mf_dca_repo_plan_create(void* db_pool, int64_t user_id, const mf_dca_plan_t* plan)
{
    char uid[32], tid[32], fid[32], amt[64], pr[64], tta[64], ttp[32], dop[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)plan->target_asset_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)plan->funding_asset_id);
    snprintf(amt, sizeof(amt), "%.4f", plan->amount);
    snprintf(pr, sizeof(pr), "%.4f", plan->target_profit_rate);
    snprintf(tta, sizeof(tta), "%.4f", plan->target_total_amount);
    snprintf(ttp, sizeof(ttp), "%lld", (long long)plan->target_total_periods);
    snprintf(dop, sizeof(dop), "%lld", (long long)plan->day_of_period);
    const char*   sql = "INSERT INTO dca_plans (user_id, target_asset_id, funding_asset_id, name, "
                        "frequency, day_of_period, amount, target_profit_rate, "
                        "target_total_amount, target_total_periods, note) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id";
    const char*   params[] = {uid,
                              tid,
                              fid,
                              plan->name ? plan->name : "",
                              plan->frequency ? plan->frequency : "monthly",
                              dop,
                              amt,
                              pr,
                              tta,
                              ttp,
                              plan->note ? plan->note : "",
                              NULL};
    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)db_pool, sql, params);
    int64_t       new_id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        new_id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return new_id;
}

int
mf_dca_repo_plan_update(void* db_pool, int64_t user_id, int64_t id, const mf_dca_plan_t* plan)
{
    char uid[32], pid[32], tid[32], fid[32], amt[64], pr[64], tta[64], ttp[32], dop[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)id);
    snprintf(tid, sizeof(tid), "%lld", (long long)plan->target_asset_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)plan->funding_asset_id);
    snprintf(amt, sizeof(amt), "%.4f", plan->amount);
    snprintf(pr, sizeof(pr), "%.4f", plan->target_profit_rate);
    snprintf(tta, sizeof(tta), "%.4f", plan->target_total_amount);
    snprintf(ttp, sizeof(ttp), "%lld", (long long)plan->target_total_periods);
    snprintf(dop, sizeof(dop), "%lld", (long long)plan->day_of_period);
    const char*   sql = "UPDATE dca_plans SET target_asset_id=?, funding_asset_id=?, name=?, "
                        "frequency=?, day_of_period=?, amount=?, target_profit_rate=?, "
                        "target_total_amount=?, target_total_periods=?, note=?, "
                        "updated_at=CURRENT_TIMESTAMP "
                        "WHERE user_id=? AND id=? RETURNING id";
    const char*   params[] = {tid,
                              fid,
                              plan->name ? plan->name : "",
                              plan->frequency ? plan->frequency : "monthly",
                              dop,
                              amt,
                              pr,
                              tta,
                              ttp,
                              plan->note ? plan->note : "",
                              uid,
                              pid,
                              NULL};
    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)db_pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
mf_dca_repo_plan_set_status(void* db_pool, int64_t user_id, int64_t id, const char* status)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)id);
    const char*   sql = "UPDATE dca_plans SET status = ?, updated_at = CURRENT_TIMESTAMP WHERE "
                        "user_id = ? AND id = ? RETURNING id";
    const char*   params[] = {status ? status : "active", uid, pid, NULL};
    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)db_pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
mf_dca_repo_plan_delete(void* db_pool, int64_t user_id, int64_t id)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)id);
    const char*   sql = "DELETE FROM dca_plans WHERE user_id = ? AND id = ? RETURNING id";
    const char*   params[] = {uid, pid, NULL};
    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)db_pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

csilk_json_t*
mf_dca_repo_plan_list_all_active(void* db_pool)
{
    const char* sql = "SELECT id, user_id, target_asset_id, funding_asset_id, name, "
                      "       frequency, day_of_period, amount, target_profit_rate, "
                      "       target_total_amount, target_total_periods, status "
                      "FROM dca_plans "
                      "WHERE status = 'active'";
    return csilk_db_query_json((csilk_db_pool_t*)db_pool, sql);
}

int64_t
mf_dca_repo_execution_create(
    void* db_pool, int64_t plan_id, int64_t user_id, const char* period_date, double planned_amount)
{
    char pid[32], uid[32], pamt[64];
    snprintf(pid, sizeof(pid), "%lld", (long long)plan_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pamt, sizeof(pamt), "%.4f", planned_amount);
    const char* sql =
        "INSERT INTO dca_executions (plan_id, user_id, period_date, planned_amount, status) "
        "VALUES (?, ?, ?, ?, 'pending') RETURNING id";
    const char*   params[] = {pid, uid, period_date ? period_date : "", pamt, NULL};
    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)db_pool, sql, params);
    int64_t       new_id = -1;
    if (res && csilk_json_array_size(res) > 0) {
        new_id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return new_id;
}

csilk_json_t*
mf_dca_repo_execution_list_by_plan(void* db_pool, int64_t user_id, int64_t plan_id)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)plan_id);
    const char* sql = "SELECT e.id, e.plan_id, e.user_id, e.period_date, e.planned_amount, "
                      "       e.actual_amount, e.executed_price, e.executed_quantity, "
                      "       e.transaction_id, e.status, "
                      "       CAST(e.created_at AS TEXT) AS created_at "
                      "FROM dca_executions e "
                      "WHERE e.user_id = ? AND e.plan_id = ? "
                      "ORDER BY e.period_date DESC, e.id DESC";
    return csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool, sql, (const char*[]){uid, pid, NULL});
}

csilk_json_t*
mf_dca_repo_execution_list_pending(void* db_pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    const char* sql = "SELECT e.id, e.plan_id, e.user_id, e.period_date, e.planned_amount, "
                      "       e.actual_amount, e.executed_price, e.executed_quantity, "
                      "       e.transaction_id, e.status, "
                      "       CAST(e.created_at AS TEXT) AS created_at "
                      "FROM dca_executions e "
                      "WHERE e.user_id = ? AND e.status = 'pending' "
                      "ORDER BY e.period_date ASC";
    return csilk_db_query_param_json((csilk_db_pool_t*)db_pool, sql, (const char*[]){uid, NULL});
}

csilk_json_t*
mf_dca_repo_execution_get(void* db_pool, int64_t user_id, int64_t id)
{
    char uid[32], eid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(eid, sizeof(eid), "%lld", (long long)id);
    const char* sql =
        "SELECT e.id, e.plan_id, e.user_id, e.period_date, e.planned_amount, "
        "       e.actual_amount, e.executed_price, e.executed_quantity, "
        "       e.transaction_id, e.status, CAST(e.created_at AS TEXT) AS created_at, "
        "       p.target_asset_id, p.funding_asset_id, p.name AS plan_name, "
        "       ta.name AS target_asset_name, ta.symbol AS target_symbol, "
        "       ta.net_value AS target_net_value, "
        "       fa.name AS funding_asset_name "
        "FROM dca_executions e "
        "JOIN dca_plans p ON p.id = e.plan_id "
        "JOIN assets ta ON ta.id = p.target_asset_id "
        "JOIN assets fa ON fa.id = p.funding_asset_id "
        "WHERE e.user_id = ? AND e.id = ?";
    csilk_json_t* arr =
        csilk_db_query_param_json((csilk_db_pool_t*)db_pool, sql, (const char*[]){uid, eid, NULL});
    if (!arr || csilk_json_array_size(arr) == 0) {
        if (arr) {
            csilk_json_free(arr);
        }
        return NULL;
    }
    return arr;
}

int
mf_dca_repo_execution_update_confirmed(void*   db_pool,
                                       int64_t id,
                                       double  actual_amount,
                                       double  executed_price,
                                       double  executed_quantity,
                                       int64_t transaction_id)
{
    char eid[32], amt[64], ep[64], eq[64], tid[32];
    snprintf(eid, sizeof(eid), "%lld", (long long)id);
    snprintf(amt, sizeof(amt), "%.4f", actual_amount);
    snprintf(ep, sizeof(ep), "%.4f", executed_price);
    snprintf(eq, sizeof(eq), "%.4f", executed_quantity);
    snprintf(tid, sizeof(tid), "%lld", (long long)transaction_id);
    const char*   sql = "UPDATE dca_executions SET actual_amount=?, executed_price=?, "
                        "executed_quantity=?, transaction_id=?, status='confirmed', "
                        "updated_at=CURRENT_TIMESTAMP WHERE id=? RETURNING id";
    const char*   params[] = {amt, ep, eq, tid, eid, NULL};
    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)db_pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

int
mf_dca_repo_execution_update_status(void* db_pool, int64_t user_id, int64_t id, const char* status)
{
    char uid[32], eid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(eid, sizeof(eid), "%lld", (long long)id);
    const char* sql = "UPDATE dca_executions SET status = ?, updated_at = CURRENT_TIMESTAMP WHERE "
                      "user_id = ? AND id = ? RETURNING id";
    const char* params[] = {status ? status : "pending", uid, eid, NULL};
    csilk_json_t* res = csilk_db_query_param_json((csilk_db_pool_t*)db_pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}
