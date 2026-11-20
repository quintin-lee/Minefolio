#pragma once

#include "csilk/csilk.h"
#include "domain/account/entity.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @file account_permission_repo_impl.h
 * @brief 账户级与字段级 RBAC 仓储 SQL 实现声明 (Infrastructure)
 */

/**
 * @brief 查询指定用户在指定账户上的显式权限配置
 * @return 单行 JSON 数组（0行表示未配置/继承），包含 permission_level, mask_balance, mask_account_no
 */
csilk_json_t*
account_perm_get(csilk_db_pool_t* pool, int64_t ledger_id, int64_t account_id, int64_t user_id);

/**
 * @brief 查询某账户的所有成员权限覆盖列表（供 Owner/Manager 查看）
 * @return JSON 数组，含 user_id, username, permission_level, mask_balance, mask_account_no, created_at, updated_at
 */
csilk_json_t*
account_perm_list_by_account(csilk_db_pool_t* pool, int64_t ledger_id, int64_t account_id);

/**
 * @brief 查询某成员在账本下所有账户的权限覆盖列表
 * @return JSON 数组，含 account_id, account_name, permission_level, mask_balance, mask_account_no, updated_at
 */
csilk_json_t* account_perm_list_by_user(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id);

/**
 * @brief 设置/更新某成员在某账户上的权限与脱敏选项
 * @return 0 成功，-1 失败
 */
int account_perm_upsert(csilk_db_pool_t* pool,
                        int64_t          ledger_id,
                        int64_t          account_id,
                        int64_t          user_id,
                        const char*      permission_level,
                        int              mask_balance,
                        int              mask_account_no);

/**
 * @brief 删除某成员在某账户上的显式权限（重置为继承账本角色）
 * @return 0 成功，-1 失败
 */
int
account_perm_delete(csilk_db_pool_t* pool, int64_t ledger_id, int64_t account_id, int64_t user_id);
