#pragma once
#include "core/financial/currency.h"
#include "core/financial/money.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include <stdint.h>

/**
 * @struct transaction_t
 * @brief 交易与买卖发生流水领域模型（Financial Core 强类型驱动）
 */
typedef struct {
    int64_t    id;
    int64_t    user_id;
    int64_t    asset_id;
    int64_t    linked_asset_id;
    int64_t    category_id;
    char       transaction_type[32];
    char       source_type[32];
    int        direction;
    int        linked_direction;
    money_t    amount;         /**< 发生总金额 */
    price_t    price_per_unit; /**< 成交单价 */
    quantity_t quantity;       /**< 成交份额数量 */
    currency_t currency;       /**< 交易币种 */
    char       transaction_date[32];
    char       note[256];
    char       created_at[64];
} transaction_t;
