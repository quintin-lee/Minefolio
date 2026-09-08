/**
 * @file repository.h
 * @brief 定投计划领域仓储契约接口 (Domain DCA Repository Contract)
 */

#pragma once

#include "domain/dca/entity.h"
#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @brief 查询用户的所有定投计划列表
 */
csilk_json_t* mf_dca_repo_plan_list(void* pool, int64_t user_id);

/**
 * @brief 获取单个定投计划详情
 */
csilk_json_t* mf_dca_repo_plan_get(void* pool, int64_t user_id, int64_t id);

/**
 * @brief 创建定投计划
 * @return 新计划 ID，失败返回 -1
 */
int64_t mf_dca_repo_plan_create(void* pool, int64_t user_id, const mf_dca_plan_t* plan);

/**
 * @brief 更新定投计划
 * @return 0: 成功, -1: 失败
 */
int mf_dca_repo_plan_update(void* pool, int64_t user_id, int64_t id, const mf_dca_plan_t* plan);

/**
 * @brief 修改定投计划状态
 */
int mf_dca_repo_plan_set_status(void* pool, int64_t user_id, int64_t id, const char* status);

/**
 * @brief 删除定投计划
 */
int mf_dca_repo_plan_delete(void* pool, int64_t user_id, int64_t id);

/**
 * @brief 查询所有活跃计划（供调度器使用）
 */
csilk_json_t* mf_dca_repo_plan_list_all_active(void* pool);

/**
 * @brief 生成定投执行记录
 * @return 新记录 ID，失败返回 -1
 */
int64_t mf_dca_repo_execution_create(
    void* pool, int64_t plan_id, int64_t user_id, const char* period_date, double planned_amount);

/**
 * @brief 查询指定计划的执行记录
 */
csilk_json_t* mf_dca_repo_execution_list_by_plan(void* pool, int64_t user_id, int64_t plan_id);

/**
 * @brief 查询用户所有待执行记录
 */
csilk_json_t* mf_dca_repo_execution_list_pending(void* pool, int64_t user_id);

/**
 * @brief 获取单条执行记录详情
 */
csilk_json_t* mf_dca_repo_execution_get(void* pool, int64_t user_id, int64_t id);

/**
 * @brief 确认执行并关联交易
 */
int mf_dca_repo_execution_update_confirmed(void*   pool,
                                           int64_t id,
                                           double  actual_amount,
                                           double  executed_price,
                                           double  executed_quantity,
                                           int64_t transaction_id);

/**
 * @brief 更新执行记录状态
 */
int
mf_dca_repo_execution_update_status(void* pool, int64_t user_id, int64_t id, const char* status);
