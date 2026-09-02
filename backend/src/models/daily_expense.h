#pragma once
#include "core/financial/currency.h"
#include "core/financial/money.h"
#include <stdint.h>

/**
 * @struct daily_expense_t
 * @brief 日常收支记账领域模型（Financial Core 强类型驱动）
 */
typedef struct {
    int64_t    id;
    int64_t    user_id;
    int64_t    category_id;
    int64_t    asset_id;
    char       expense_type[16];
    money_t    amount;   /**< 采用精确 money_t 表示金额，彻底杜绝 double 浮点误差 */
    currency_t currency; /**< 币种强类型 */
    char       expense_date[32];
    char       note[256];
    char       created_at[64];
    char       updated_at[64];
} daily_expense_t;
