#pragma once

#include <stdint.h>
#include "application/account/commands.h"
#include "application/account/dtos.h"
#include "csilk/csilk.h"

/**
 * @brief 创建账户
 */
int account_usecase_create(void*                       pool,
                           const create_account_cmd_t* cmd,
                           account_usecase_result_t*   out_res);

/**
 * @brief 更新账户基础属性 / 归档标志
 */
int account_usecase_update(void*                       pool,
                           const update_account_cmd_t* cmd,
                           account_usecase_result_t*   out_res);

/**
 * @brief 删除账户（有资产或交易时拒绝，引导归档）
 */
int account_usecase_delete(void*                       pool,
                           const delete_account_cmd_t* cmd,
                           account_usecase_result_t*   out_res);

/**
 * @brief 归档/恢复账户
 */
int account_usecase_archive(
    void* pool, int64_t user_id, int64_t id, int archived, account_usecase_result_t* out_res);

/**
 * @brief 分页查询账户列表（返回行含 asset_count / total_value）
 */
int account_usecase_query(void*          pool,
                          int64_t        user_id,
                          int64_t        ledger_id,
                          int            include_archived,
                          csilk_json_t** out_list,
                          int64_t*       out_total);

/**
 * @brief 幂等创建「默认账户」（懒种子钩子，对齐 category_usecase_seed_defaults）
 */
void account_usecase_seed_default(void* pool, int64_t user_id, int64_t ledger_id);
