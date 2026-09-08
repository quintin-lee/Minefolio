/**
 * @file usecases.h
 * @brief 定投计划用例接口声明 (Application DCA Use Cases)
 */

#pragma once

#include "application/dca/commands.h"
#include "application/dca/dtos.h"
#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @brief 列出用户所有定投计划（含收益率计算）
 */
csilk_json_t* dca_usecase_list_plans(void* pool, int64_t user_id);

/**
 * @brief 获取单个定投计划详情
 */
csilk_json_t*
dca_usecase_get_plan(void* pool, int64_t user_id, int64_t id, dca_usecase_result_t* out_res);

/**
 * @brief 创建定投计划
 */
int64_t dca_usecase_create_plan(void*                        pool,
                                const create_dca_plan_cmd_t* cmd,
                                dca_usecase_result_t*        out_res);

/**
 * @brief 更新定投计划
 */
int dca_usecase_update_plan(void*                        pool,
                            const update_dca_plan_cmd_t* cmd,
                            dca_usecase_result_t*        out_res);

/**
 * @brief 设置定投计划状态
 */
int dca_usecase_set_plan_status(
    void* pool, int64_t user_id, int64_t id, const char* status, dca_usecase_result_t* out_res);

/**
 * @brief 删除定投计划
 */
int dca_usecase_delete_plan(void* pool, int64_t user_id, int64_t id, dca_usecase_result_t* out_res);

/**
 * @brief 列出计划执行记录
 */
csilk_json_t* dca_usecase_list_executions(void* pool, int64_t user_id, int64_t plan_id);

/**
 * @brief 列出所有待执行记录
 */
csilk_json_t* dca_usecase_list_pending(void* pool, int64_t user_id);

/**
 * @brief 确认定投执行（含交易记录创建）
 */
int dca_usecase_confirm_execution(void*                              pool,
                                  const confirm_dca_execution_cmd_t* cmd,
                                  dca_confirm_result_t*              out_res);

/**
 * @brief 跳过定投执行
 */
int dca_usecase_skip_execution(void*                 pool,
                               int64_t               user_id,
                               int64_t               exec_id,
                               dca_usecase_result_t* out_res);
