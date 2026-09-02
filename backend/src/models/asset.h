#pragma once
#include "core/financial/currency.h"
#include "core/financial/money.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include <stdint.h>

/**
 * @struct asset_t
 * @brief 资产与投资持仓领域模型（Financial Core 强类型驱动）
 */
typedef struct {
    int64_t    id;
    int64_t    user_id;
    int64_t    category_id;
    char       name[128];
    char       account_no[64];
    money_t    current_value; /**< 当前市值/余额 */
    currency_t currency;      /**< 资产结算币种 */
    char       note[256];
    quantity_t quantity;      /**< 标的持仓份额/数量 */
    money_t    cost_basis;    /**< 累计建仓与手续费成本基础 */
    price_t    net_value;     /**< 最新单位净值/市价 */
    char       created_at[64];
    char       updated_at[64];
} asset_t;
