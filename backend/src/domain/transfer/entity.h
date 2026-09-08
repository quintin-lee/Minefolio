/**
 * @file entity.h
 * @brief 转账领域实体定义 (Domain Transfer Entity)
 */

#pragma once

#include <stdint.h>

/**
 * @brief 转账记录实体
 */
typedef struct {
    int64_t id;                /**< 主键 ID */
    int64_t user_id;           /**< 用户 ID */
    int64_t from_asset_id;     /**< 转出资产 ID */
    int64_t to_asset_id;       /**< 转入资产 ID */
    double  amount;            /**< 划转金额 */
    char    currency[8];       /**< 货币代码 */
    char    transfer_date[16]; /**< 转账日期 (YYYY-MM-DD) */
    char    note[256];         /**< 备注 */
    char    created_at[32];    /**< 创建时间 */
} mf_transfer_t;
