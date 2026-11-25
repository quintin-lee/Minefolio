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
 * @brief 账户级读可见性 SQL 谓词（过滤 permission_level = 'none' 的非所有者账户）
 *        需要绑定 2 个参数：[user_id, user_id]
 */
#define MF_RBAC_ASSET_VISIBLE_SQL(alias)                                                           \
    "(" alias ".account_id IS NULL OR " alias ".account_id <= 0 OR "                               \
    "EXISTS (SELECT 1 FROM accounts _ac JOIN ledgers _l ON _l.id = _ac.ledger_id WHERE _ac.id "    \
    "= " alias ".account_id AND _l.owner_id = ?) OR "                                              \
    "NOT EXISTS (SELECT 1 FROM account_permissions _ap WHERE _ap.account_id = " alias              \
    ".account_id AND _ap.user_id = ? AND _ap.permission_level = 'none'))"

/**
 * @brief 聚合报表未脱敏余额 SQL 谓词（排除 'none' 与 mask_balance != 0 的非所有者账户）
 *        需要绑定 2 个参数：[user_id, user_id]
 */
#define MF_RBAC_ASSET_UNMASKED_SQL(alias)                                                          \
    "(" alias ".account_id IS NULL OR " alias ".account_id <= 0 OR "                               \
    "EXISTS (SELECT 1 FROM accounts _ac JOIN ledgers _l ON _l.id = _ac.ledger_id WHERE _ac.id "    \
    "= " alias ".account_id AND _l.owner_id = ?) OR "                                              \
    "NOT EXISTS (SELECT 1 FROM account_permissions _ap WHERE _ap.account_id = " alias              \
    ".account_id AND _ap.user_id = ? AND (_ap.permission_level = 'none' OR _ap.mask_balance != "   \
    "0)))"

/**
 * @brief 共享账本成员资产归属可见性谓词（需要绑定 4 个参数：[ledger_id, user_id, user_id, user_id]）
 */
#define MF_LEDGER_SHARED_ACCESS_SQL(alias)                                                         \
    "(" alias ".ledger_id = ? AND (" alias ".user_id = ? OR EXISTS (SELECT 1 FROM ledgers _l "     \
    "WHERE _l.id = " alias ".ledger_id AND _l.owner_id = ?) OR EXISTS (SELECT 1 FROM "             \
    "ledger_members _lm WHERE _lm.ledger_id = " alias ".ledger_id AND _lm.user_id = ?)))"

/**
 * @brief 共享账本成员余额变动日志归属谓词（需要绑定 4 个参数：[ledger_id, user_id, user_id, user_id]）
 */
#define MF_LEDGER_SHARED_BALANCE_LOG_SQL(log_alias, asset_alias)                                   \
    "(" asset_alias ".ledger_id = ? AND (" log_alias ".user_id = ? OR EXISTS (SELECT 1 FROM "      \
    "ledgers _l WHERE _l.id = " asset_alias ".ledger_id AND _l.owner_id = ?) OR EXISTS (SELECT 1 " \
    "FROM ledger_members _lm WHERE _lm.ledger_id = " asset_alias                                   \
    ".ledger_id AND _lm.user_id = ?)))"

/**
 * @brief 资产归属账户是否对当前用户开启了金额脱敏（非所有者且 mask_balance != 0）
 *        需要绑定 2 个参数：[user_id, user_id]
 */
#define MF_RBAC_MASK_BALANCE_COL(alias)                                                            \
    "(CASE WHEN " alias ".account_id IS NOT NULL AND " alias ".account_id > 0 "                    \
    "AND NOT EXISTS (SELECT 1 FROM accounts _ac JOIN ledgers _l ON _l.id = _ac.ledger_id WHERE "   \
    "_ac.id = " alias ".account_id AND _l.owner_id = ?) "                                          \
    "AND EXISTS (SELECT 1 FROM account_permissions _ap WHERE _ap.account_id = " alias              \
    ".account_id AND _ap.user_id = ? AND _ap.mask_balance != 0) THEN 1 ELSE 0 END)"

/**
 * @brief 资产归属账户是否对当前用户开启了卡号脱敏（非所有者且 mask_account_no != 0）
 *        需要绑定 2 个参数：[user_id, user_id]
 */
#define MF_RBAC_MASK_ACCOUNT_NO_COL(alias)                                                         \
    "(CASE WHEN " alias ".account_id IS NOT NULL AND " alias ".account_id > 0 "                    \
    "AND NOT EXISTS (SELECT 1 FROM accounts _ac JOIN ledgers _l ON _l.id = _ac.ledger_id WHERE "   \
    "_ac.id = " alias ".account_id AND _l.owner_id = ?) "                                          \
    "AND EXISTS (SELECT 1 FROM account_permissions _ap WHERE _ap.account_id = " alias              \
    ".account_id AND _ap.user_id = ? AND _ap.mask_account_no != 0) THEN 1 ELSE 0 END)"

/**
 * @brief 查询用户对指定账户的有效字段脱敏标志（账本所有者恒不脱敏）
 * @return 0 成功，-1 参数或查询错误
 */
int account_perm_get_effective_masks(csilk_db_pool_t* pool,
                                     int64_t          ledger_id,
                                     int64_t          account_id,
                                     int64_t          user_id,
                                     int*             out_mask_balance,
                                     int*             out_mask_account_no);

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
