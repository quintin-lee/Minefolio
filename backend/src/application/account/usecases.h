#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "application/account/commands.h"
#include "application/account/dtos.h"
#include "domain/account/entity.h"
#include "csilk/csilk.h"

/**
 * @brief 账户级操作类型定义
 */
typedef enum {
    MF_ACCOUNT_ACTION_READ = 0,     /**< 读取/查看账户及下属资产、交易 */
    MF_ACCOUNT_ACTION_WRITE_TX = 1, /**< 流水记账（收支、交易、调拨转账） */
    MF_ACCOUNT_ACTION_MANAGE = 2    /**< 账户管理（修改属性、配置规则、删除账户） */
} mf_account_action_t;

/**
 * @brief 校验用户对某账户在账本中的操作权限
 * @return 0: 允许; 1004: 权限不足 / 拒绝; 1003: 不存在
 */
int account_usecase_check_permission(
    void* pool, int64_t ledger_id, int64_t user_id, int64_t account_id, mf_account_action_t action);

/**
 * @brief 查询单条账户详情（含字段级脱敏及权限标识）
 */
int account_usecase_get(void*                     pool,
                        int64_t                   user_id,
                        int64_t                   ledger_id,
                        int64_t                   account_id,
                        csilk_json_t**            out_obj,
                        account_usecase_result_t* out_res);

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
 * @brief 分页查询账户列表（返回行含 asset_count / total_value 及脱敏标识）
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

/**
 * @brief 查询某账户的所有成员权限覆盖列表（仅 Owner 允许）
 */
int account_permission_usecase_list_by_account(void*                     pool,
                                               int64_t                   user_id,
                                               int64_t                   ledger_id,
                                               int64_t                   account_id,
                                               csilk_json_t**            out_list,
                                               account_usecase_result_t* out_res);

/**
 * @brief 查询某成员在某账本下的所有账户权限覆盖列表（仅 Owner 允许）
 */
int account_permission_usecase_list_by_user(void*                     pool,
                                            int64_t                   user_id,
                                            int64_t                   ledger_id,
                                            int64_t                   target_user_id,
                                            csilk_json_t**            out_list,
                                            account_usecase_result_t* out_res);

/**
 * @brief 设置某成员在某账户上的权限配置（仅 Owner 允许）
 */
int account_permission_usecase_set(void*                     pool,
                                   int64_t                   user_id,
                                   int64_t                   ledger_id,
                                   int64_t                   account_id,
                                   int64_t                   target_user_id,
                                   const char*               permission_level,
                                   int                       mask_balance,
                                   int                       mask_account_no,
                                   account_usecase_result_t* out_res);

/**
 * @brief 批量设置某账户的多成员权限配置（仅 Owner 允许）
 */
int account_permission_usecase_batch_set(void*                     pool,
                                         int64_t                   user_id,
                                         int64_t                   ledger_id,
                                         int64_t                   account_id,
                                         const csilk_json_t*       rules_arr,
                                         account_usecase_result_t* out_res);

/**
 * @brief 批量设置某成员在账本中多个账户的权限配置（仅 Owner 允许）
 */
int account_permission_usecase_batch_set_for_user(void*                     pool,
                                                  int64_t                   user_id,
                                                  int64_t                   ledger_id,
                                                  int64_t                   target_user_id,
                                                  const csilk_json_t*       rules_arr,
                                                  account_usecase_result_t* out_res);

/**
 * @brief 删除显式权限配置，恢复继承（仅 Owner 允许）
 */
int account_permission_usecase_delete(void*                     pool,
                                      int64_t                   user_id,
                                      int64_t                   ledger_id,
                                      int64_t                   account_id,
                                      int64_t                   target_user_id,
                                      account_usecase_result_t* out_res);
