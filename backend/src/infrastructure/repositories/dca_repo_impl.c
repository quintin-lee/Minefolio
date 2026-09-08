/**
 * @file dca_repo_impl.c
 * @brief 定投计划仓储 SQL 实现 (Infrastructure DCA Repository)
 *
 * 包装 repositories/dca_repo.c 中的现有 SQL 函数。
 */

#include "infrastructure/repositories/dca_repo_impl.h"
#include "repositories/dca_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

csilk_json_t*
mf_dca_repo_plan_list(void* db_pool, int64_t user_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_plan_list(pool, user_id);
}

csilk_json_t*
mf_dca_repo_plan_get(void* db_pool, int64_t user_id, int64_t id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_plan_get(pool, user_id, id);
}

int64_t
mf_dca_repo_plan_create(void* db_pool, int64_t user_id, const mf_dca_plan_t* plan)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_plan_create(pool,
                           user_id,
                           plan->target_asset_id,
                           plan->funding_asset_id,
                           plan->name,
                           plan->frequency,
                           plan->day_of_period,
                           plan->amount,
                           plan->target_profit_rate,
                           plan->target_total_amount,
                           plan->target_total_periods,
                           plan->note);
}

int
mf_dca_repo_plan_update(void* db_pool, int64_t user_id, int64_t id, const mf_dca_plan_t* plan)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_plan_update(pool,
                           user_id,
                           id,
                           plan->target_asset_id,
                           plan->funding_asset_id,
                           plan->name,
                           plan->frequency,
                           plan->day_of_period,
                           plan->amount,
                           plan->target_profit_rate,
                           plan->target_total_amount,
                           plan->target_total_periods,
                           plan->note);
}

int
mf_dca_repo_plan_set_status(void* db_pool, int64_t user_id, int64_t id, const char* status)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_plan_set_status(pool, user_id, id, status);
}

int
mf_dca_repo_plan_delete(void* db_pool, int64_t user_id, int64_t id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_plan_delete(pool, user_id, id);
}

csilk_json_t*
mf_dca_repo_plan_list_all_active(void* db_pool)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_plan_list_all_active(pool);
}

int64_t
mf_dca_repo_execution_create(
    void* db_pool, int64_t plan_id, int64_t user_id, const char* period_date, double planned_amount)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_execution_create(pool, plan_id, user_id, period_date, planned_amount);
}

csilk_json_t*
mf_dca_repo_execution_list_by_plan(void* db_pool, int64_t user_id, int64_t plan_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_execution_list_by_plan(pool, user_id, plan_id);
}

csilk_json_t*
mf_dca_repo_execution_list_pending(void* db_pool, int64_t user_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_execution_list_pending(pool, user_id);
}

csilk_json_t*
mf_dca_repo_execution_get(void* db_pool, int64_t user_id, int64_t id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_execution_get(pool, user_id, id);
}

int
mf_dca_repo_execution_update_confirmed(void*   db_pool,
                                       int64_t id,
                                       double  actual_amount,
                                       double  executed_price,
                                       double  executed_quantity,
                                       int64_t transaction_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_execution_update_confirmed(
        pool, id, actual_amount, executed_price, executed_quantity, transaction_id);
}

int
mf_dca_repo_execution_update_status(void* db_pool, int64_t user_id, int64_t id, const char* status)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return dca_execution_update_status(pool, user_id, id, status);
}
