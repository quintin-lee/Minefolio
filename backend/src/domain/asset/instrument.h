#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "core/financial/currency.h"

/**
 * @brief 纯标的定义实体 (Instrument Asset Definition)
 * 标的本身是客观金融工具/证券，不包含账户信息、持仓份额或成本
 */
typedef struct {
    int64_t    id;               /**< 标的唯一 ID */
    char       symbol[32];       /**< 代码 (如 "AAPL", "600519", "BTC") */
    char       name[128];        /**< 标的名称 (如 "贵州茅台") */
    char       asset_type[32];   /**< 标的类别: "stock", "fund", "bond", "crypto" */
    currency_t native_currency;  /**< 原生计价货币 (如 USD, CNY) */
    char       quote_source[32]; /**< 行情源 (如 "stock_cn", "yahoo") */
    char       note[256];
} mf_instrument_asset_t;

static inline bool
mf_instrument_is_investment(const char* type)
{
    if (!type || !type[0]) {
        return false;
    }
    return (strcmp(type, "stock") == 0 || strcmp(type, "fund") == 0 || strcmp(type, "bond") == 0 ||
            strcmp(type, "crypto") == 0);
}
