/**
 * @file dca_repo.c
 * @brief 自动定投计划与执行记录数据访问层具体实现
 *
 * 实现了定投计划配置 CRUD、多表 JOIN 联查标的持仓与扣款账户信息、
 * 子查询统计定投执行期数与累计投入，以及定投期次生成、状态机流转与成交确认回填。
 */

#include "repositories/dca_repo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 查询指定用户的所有定投计划
 *
 * 执行 JOIN 查询关联目标资产与资金资产，并通过内联子查询统计：
 * - `executed_periods`: 已确认期数 (`status='confirmed'`)
 * - `total_invested_amount`: 累计投入金额
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 定投计划列表 JSON 数组
 */
csilk_json_t*
dca_plan_list(csilk_db_pool_t* pool, int64_t user_id)
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

    return csilk_db_query_param_json(pool, sql, (const char*[]){uid, NULL});
}

/**
 * @brief 查询单条定投计划详情
 *
 * 执行 SQL 联查指定 `p.id` 与 `p.user_id` 的记录。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 计划 ID
 * @return csilk_json_t* 包含计划详情的 JSON 数组（未找到返回 NULL）
 */
csilk_json_t*
dca_plan_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
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

    csilk_json_t* arr = csilk_db_query_param_json(pool, sql, (const char*[]){uid, idstr, NULL});
    if (!arr || csilk_json_array_size(arr) == 0) {
        if (arr) {
            csilk_json_free(arr);
        }
        return NULL;
    }
    return arr;
}

/**
 * @brief 插入新的定投计划
 *
 * 执行 SQL：
 * `INSERT INTO dca_plans (...) VALUES (...) RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param target_asset_id 目标资产 ID
 * @param funding_asset_id 扣款账户 ID
 * @param name 计划名称
 * @param frequency 周期频率
 * @param day_of_period 扣款日
 * @param amount 计划定投额
 * @param target_profit_rate 目标止盈率
 * @param target_total_amount 目标总金额
 * @param target_total_periods 目标总期数
 * @param note 备注
 * @return int64_t 成功返回新生成主键 ID，失败返回 -1
 */
int64_t
dca_plan_create(csilk_db_pool_t* pool,
                int64_t          user_id,
                int64_t          target_asset_id,
                int64_t          funding_asset_id,
                const char*      name,
                const char*      frequency,
                int              day_of_period,
                double           amount,
                double           target_profit_rate,
                double           target_total_amount,
                int              target_total_periods,
                const char*      note)
{
    char uid[32], tid[32], fid[32], dop[32], amt[64], tpr[64], tta[64], ttp[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)target_asset_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)funding_asset_id);
    snprintf(dop, sizeof(dop), "%d", day_of_period);
    snprintf(amt, sizeof(amt), "%.4f", amount);
    snprintf(tpr, sizeof(tpr), "%.4f", target_profit_rate);
    snprintf(tta, sizeof(tta), "%.4f", target_total_amount);
    snprintf(ttp, sizeof(ttp), "%d", target_total_periods);

    const char* sql =
        "INSERT INTO dca_plans (user_id, target_asset_id, funding_asset_id, name, frequency, "
        "                       day_of_period, amount, target_profit_rate, target_total_amount, "
        "                       target_total_periods, status, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'active', ?) RETURNING id";

    const char* params[] = {uid,
                            tid,
                            fid,
                            name ? name : "",
                            frequency ? frequency : "monthly",
                            dop,
                            amt,
                            tpr,
                            tta,
                            ttp,
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

/**
 * @brief 更新定投计划配置
 *
 * 执行 SQL：`UPDATE dca_plans SET ... WHERE user_id = ? AND id = ? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 计划 ID
 * @param target_asset_id 目标资产 ID
 * @param funding_asset_id 扣款账户 ID
 * @param name 计划名称
 * @param frequency 周期频率
 * @param day_of_period 扣款日
 * @param amount 定投金额
 * @param target_profit_rate 目标止盈率
 * @param target_total_amount 目标总金额
 * @param target_total_periods 目标总期数
 * @param note 备注
 * @return int 成功返回 0，失败返回 -1
 */
int
dca_plan_update(csilk_db_pool_t* pool,
                int64_t          user_id,
                int64_t          id,
                int64_t          target_asset_id,
                int64_t          funding_asset_id,
                const char*      name,
                const char*      frequency,
                int              day_of_period,
                double           amount,
                double           target_profit_rate,
                double           target_total_amount,
                int              target_total_periods,
                const char*      note)
{
    char uid[32], pid[32], tid[32], fid[32], dop[32], amt[64], tpr[64], tta[64], ttp[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)id);
    snprintf(tid, sizeof(tid), "%lld", (long long)target_asset_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)funding_asset_id);
    snprintf(dop, sizeof(dop), "%d", day_of_period);
    snprintf(amt, sizeof(amt), "%.4f", amount);
    snprintf(tpr, sizeof(tpr), "%.4f", target_profit_rate);
    snprintf(tta, sizeof(tta), "%.4f", target_total_amount);
    snprintf(ttp, sizeof(ttp), "%d", target_total_periods);

    const char* sql = "UPDATE dca_plans "
                      "SET target_asset_id = ?, funding_asset_id = ?, name = ?, frequency = ?, "
                      "    day_of_period = ?, amount = ?, target_profit_rate = ?, "
                      "    target_total_amount = ?, target_total_periods = ?, note = ?, updated_at "
                      "= CURRENT_TIMESTAMP "
                      "WHERE user_id = ? AND id = ? RETURNING id";

    const char* params[] = {tid,
                            fid,
                            name ? name : "",
                            frequency ? frequency : "monthly",
                            dop,
                            amt,
                            tpr,
                            tta,
                            ttp,
                            note ? note : "",
                            uid,
                            pid,
                            NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

/**
 * @brief 更新定投计划状态
 *
 * 执行 SQL：`UPDATE dca_plans SET status = ?, updated_at = CURRENT_TIMESTAMP WHERE user_id = ? AND id = ? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 计划 ID
 * @param status 目标状态
 * @return int 成功返回 0，失败返回 -1
 */
int
dca_plan_set_status(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* status)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)id);

    const char* sql = "UPDATE dca_plans "
                      "SET status = ?, updated_at = CURRENT_TIMESTAMP "
                      "WHERE user_id = ? AND id = ? RETURNING id";

    const char* params[] = {status ? status : "active", uid, pid, NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

/**
 * @brief 删除定投计划
 *
 * 执行 SQL：`DELETE FROM dca_plans WHERE user_id = ? AND id = ? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 计划 ID
 * @return int 成功返回 0，失败返回 -1
 */
int
dca_plan_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)id);

    const char* sql = "DELETE FROM dca_plans WHERE user_id = ? AND id = ? RETURNING id";
    const char* params[] = {uid, pid, NULL};

    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

/**
 * @brief 查询所有处于激活状态的定投计划
 *
 * 执行 SQL：`SELECT ... FROM dca_plans WHERE status = 'active'`
 *
 * @param pool 数据库连接池指针
 * @return csilk_json_t* 活跃计划列表 JSON 数组
 */
csilk_json_t*
dca_plan_list_all_active(csilk_db_pool_t* pool)
{
    const char* sql = "SELECT id, user_id, target_asset_id, funding_asset_id, name, "
                      "       frequency, day_of_period, amount, target_profit_rate, "
                      "       target_total_amount, target_total_periods, status "
                      "FROM dca_plans "
                      "WHERE status = 'active'";
    return csilk_db_query_json(pool, sql);
}

/**
 * @brief 创建待执行定投期次记录
 *
 * 执行 SQL：`INSERT INTO dca_executions (plan_id, user_id, period_date, planned_amount, status) VALUES (?, ?, ?, ?, 'pending') RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param plan_id 定投计划 ID
 * @param user_id 用户 ID
 * @param period_date 执行日期
 * @param planned_amount 计划买入金额
 * @return int64_t 成功生成的主键 ID，失败返回 -1
 */
int64_t
dca_execution_create(csilk_db_pool_t* pool,
                     int64_t          plan_id,
                     int64_t          user_id,
                     const char*      period_date,
                     double           planned_amount)
{
    char pid[32], uid[32], pamt[64];
    snprintf(pid, sizeof(pid), "%lld", (long long)plan_id);
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pamt, sizeof(pamt), "%.4f", planned_amount);

    const char* sql =
        "INSERT INTO dca_executions (plan_id, user_id, period_date, planned_amount, status) "
        "VALUES (?, ?, ?, ?, 'pending') RETURNING id";

    const char* params[] = {pid, uid, period_date ? period_date : "", pamt, NULL};

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

/**
 * @brief 按计划查询历史执行记录
 *
 * 执行 SQL：`SELECT ... FROM dca_executions WHERE user_id = ? AND plan_id = ? ORDER BY period_date DESC, id DESC`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param plan_id 计划 ID
 * @return csilk_json_t* 执行记录列表 JSON 数组
 */
csilk_json_t*
dca_execution_list_by_plan(csilk_db_pool_t* pool, int64_t user_id, int64_t plan_id)
{
    char uid[32], pid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(pid, sizeof(pid), "%lld", (long long)plan_id);

    const char* sql = "SELECT id, plan_id, user_id, period_date, planned_amount, "
                      "       actual_amount, executed_price, executed_quantity, "
                      "       transaction_id, status, CAST(created_at AS TEXT) AS created_at, "
                      "CAST(updated_at AS TEXT) AS updated_at "
                      "FROM dca_executions "
                      "WHERE user_id = ? AND plan_id = ? "
                      "ORDER BY period_date DESC, id DESC";

    const char* params[] = {uid, pid, NULL};
    return csilk_db_query_param_json(pool, sql, params);
}

/**
 * @brief 查询用户所有待执行的定投期次
 *
 * 执行 SQL JOIN 查询关联计划与资产：`WHERE e.user_id = ? AND e.status = 'pending' ORDER BY e.period_date ASC, e.id ASC`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 待执行记录列表 JSON 数组
 */
csilk_json_t*
dca_execution_list_pending(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);

    const char* sql = "SELECT e.id, e.plan_id, e.user_id, e.period_date, e.planned_amount, "
                      "       e.status, CAST(e.created_at AS TEXT) AS created_at, "
                      "       p.name AS plan_name, p.target_asset_id, p.funding_asset_id, "
                      "       p.target_profit_rate, "
                      "       ta.name AS target_asset_name, ta.symbol AS target_symbol, "
                      "       ta.net_value AS target_net_value, ta.currency AS target_currency, "
                      "       fa.name AS funding_asset_name, fa.currency AS funding_currency "
                      "FROM dca_executions e "
                      "JOIN dca_plans p ON p.id = e.plan_id "
                      "JOIN assets ta ON ta.id = p.target_asset_id "
                      "JOIN assets fa ON fa.id = p.funding_asset_id "
                      "WHERE e.user_id = ? AND e.status = 'pending' "
                      "ORDER BY e.period_date ASC, e.id ASC";

    const char* params[] = {uid, NULL};
    return csilk_db_query_param_json(pool, sql, params);
}

/**
 * @brief 获取单个定投执行记录详情
 *
 * 执行 SQL JOIN 查询：`WHERE e.user_id = ? AND e.id = ?`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 执行记录 ID
 * @return csilk_json_t* 包含单条执行记录详情的 JSON 数组
 */
csilk_json_t*
dca_execution_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)
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

    const char* params[] = {uid, eid, NULL};
    return csilk_db_query_param_json(pool, sql, params);
}

/**
 * @brief 确认定投期次执行完成并关联交易流水
 *
 * 执行 SQL：`UPDATE dca_executions SET actual_amount = ?, executed_price = ?, executed_quantity = ?, transaction_id = ?, status = 'confirmed', updated_at = CURRENT_TIMESTAMP WHERE id = ? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param id 执行记录 ID
 * @param actual_amount 实际成交金额
 * @param executed_price 成交价格
 * @param executed_quantity 成交份额
 * @param transaction_id 关联交易 ID
 * @return int 成功返回 0，失败返回 -1
 */
int
dca_execution_update_confirmed(csilk_db_pool_t* pool,
                               int64_t          id,
                               double           actual_amount,
                               double           executed_price,
                               double           executed_quantity,
                               int64_t          transaction_id)
{
    char eid[32], aamt[64], eprice[64], eqty[64], txid[32];
    snprintf(eid, sizeof(eid), "%lld", (long long)id);
    snprintf(aamt, sizeof(aamt), "%.4f", actual_amount);
    snprintf(eprice, sizeof(eprice), "%.4f", executed_price);
    snprintf(eqty, sizeof(eqty), "%.4f", executed_quantity);
    snprintf(txid, sizeof(txid), "%lld", (long long)transaction_id);

    const char* sql =
        "UPDATE dca_executions "
        "SET actual_amount = ?, executed_price = ?, executed_quantity = ?, "
        "    transaction_id = ?, status = 'confirmed', updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ? RETURNING id";

    const char*   params[] = {aamt, eprice, eqty, txid, eid, NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}

/**
 * @brief 更新定投执行期次状态
 *
 * 执行 SQL：`UPDATE dca_executions SET status = ?, updated_at = CURRENT_TIMESTAMP WHERE user_id = ? AND id = ? RETURNING id`
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 执行记录 ID
 * @param status 目标状态 (如 pending, skipped, failed)
 * @return int 成功返回 0，失败返回 -1
 */
int
dca_execution_update_status(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* status)
{
    char uid[32], eid[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(eid, sizeof(eid), "%lld", (long long)id);

    const char* sql = "UPDATE dca_executions "
                      "SET status = ?, updated_at = CURRENT_TIMESTAMP "
                      "WHERE user_id = ? AND id = ? RETURNING id";

    const char*   params[] = {status ? status : "pending", uid, eid, NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int           ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok ? 0 : -1;
}
