/**
 * @file usecases.h
 * @brief 日常收支用例接口声明 (Application Daily Expense Use Cases)
 */

#pragma once

#include "application/daily_expense/commands.h"
#include "application/daily_expense/dtos.h"
#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @brief 分页查询日常收支列表
 */
int daily_expense_usecase_list(void*          pool,
                               int64_t        user_id,
                               int64_t        ledger_id,
                               int64_t        page,
                               int64_t        page_size,
                               const char*    expense_type,
                               const char*    category_id,
                               const char*    tag_ids,
                               const char*    start_date,
                               const char*    end_date,
                               csilk_json_t** out_json,
                               int64_t*       out_total);

/**
 * @brief 月度聚合统计
 */
int daily_expense_usecase_monthly(void*                           pool,
                                  int64_t                         user_id,
                                  int64_t                         ledger_id,
                                  int64_t                         year,
                                  int64_t                         month,
                                  daily_expense_monthly_result_t* out_res,
                                  csilk_json_t**                  out_by_category,
                                  csilk_json_t**                  out_by_tag,
                                  csilk_json_t**                  out_daily);

/**
 * @brief 创建日常收支记录（含余额调整与标签绑定）
 */
int daily_expense_usecase_create(void*                             pool,
                                 const create_daily_expense_cmd_t* cmd,
                                 const csilk_json_t*               tags,
                                 daily_expense_usecase_result_t*   out_res);

/**
 * @brief 更新日常收支记录（含余额回滚与重新应用）
 */
int daily_expense_usecase_update(void*                             pool,
                                 const update_daily_expense_cmd_t* cmd,
                                 const csilk_json_t*               tags,
                                 daily_expense_usecase_result_t*   out_res);

/**
 * @brief 删除日常收支记录（含余额回滚）
 */
int daily_expense_usecase_delete(void*                           pool,
                                 int64_t                         user_id,
                                 int64_t                         ledger_id,
                                 int64_t                         id,
                                 daily_expense_usecase_result_t* out_res);
