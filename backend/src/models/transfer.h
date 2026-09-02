#pragma once
#include "core/financial/currency.h"
#include "core/financial/money.h"
#include <stdint.h>

/**
 * @struct transfer_t
 * @brief 内部转账流水领域模型（Financial Core 强类型驱动）
 */
typedef struct {
    int64_t    id;
    int64_t    user_id;
    int64_t    from_asset_id;
    int64_t    to_asset_id;
    money_t    amount;   /**< 转账金额 */
    currency_t currency; /**< 转账币种 */
    char       transfer_date[32];
    char       note[256];
    char       created_at[64];
} transfer_t;
