/**
 * @file entity.h
 * @brief 日常收支领域实体定义 (Domain Daily Expense Entity)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 日常收支记录实体
 */
typedef struct {
    int64_t id;               /**< 主键 ID */
    int64_t user_id;          /**< 用户 ID */
    int64_t ledger_id;        /**< 账本 ID */
    int64_t category_id;      /**< 分类 ID */
    int64_t asset_id;         /**< 资产账户 ID */
    char    expense_type[16]; /**< 收支类型 ("income", "expense") */
    double  amount;           /**< 金额 */
    char    currency[8];      /**< 货币代码 */
    char    expense_date[16]; /**< 记账日期 (YYYY-MM-DD) */
    char    note[256];        /**< 备注 */
    char    created_at[32];   /**< 创建时间 */
    char    updated_at[32];   /**< 更新时间 */
} mf_daily_expense_t;

/**
 * @brief 日常收支记录的回滚属性（供余额调整使用）
 */
typedef struct {
    double  amount;
    char    expense_type[16];
    char    currency[8];
    int64_t asset_id;
    char    note[256];
} mf_daily_expense_snapshot_t;
