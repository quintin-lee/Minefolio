#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 账户实体（纯容器：不记余额，账户小计 = 子资产 SUM(current_value)）
 * @note 每个 (user, ledger) 至少有一个「默认账户」收纳未显式挂账户的资产。
 */
typedef struct {
    int64_t id;
    int64_t user_id;
    int64_t ledger_id;
    char    name[128];
    char
        institution[32]; /**< bank_card / platform / broker / fund_platform / cash_wallet / other */
    char    account_no[64];
    char    currency[16];
    char    icon[64];
    char    note[256];
    int64_t sort_order;
    bool    archived;
} mf_account_t;

/**
 * @brief 账户级 RBAC 权限级别枚举
 */
typedef enum {
    MF_ACCOUNT_PERM_INHERIT = 0, /**< 继承账本级角色 */
    MF_ACCOUNT_PERM_NONE = 1,    /**< 完全隔离 (不可见、不可写) */
    MF_ACCOUNT_PERM_VIEWER = 2, /**< 只读观察员 (可见、不可记账、不可修改账户) */
    MF_ACCOUNT_PERM_OPERATOR = 3, /**< 记账操作员 (可见、可写流水、不可修改/删除账户) */
    MF_ACCOUNT_PERM_MANAGER = 4 /**< 账户管理员 (完全控制该账户与下属资产) */
} mf_account_perm_level_t;

/**
 * @brief 账户级与字段级权限配置实体
 */
typedef struct {
    int64_t                 id;
    int64_t                 ledger_id;
    int64_t                 account_id;
    int64_t                 user_id;
    mf_account_perm_level_t level;
    bool                    mask_balance;
    bool                    mask_account_no;
    char                    created_at[32];
    char                    updated_at[32];
} mf_account_permission_t;
