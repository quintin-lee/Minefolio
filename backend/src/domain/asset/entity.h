#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "core/financial/currency.h"
#include "core/financial/money.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include "domain/asset/instrument.h"
#include "domain/asset/account.h"
#include "domain/asset/cost_basis.h"
#include "domain/asset/valuation.h"
#include "domain/asset/pnl.h"
#include "domain/asset/position.h"

/**
 * @brief 纯业务领域资产聚合根/实体 (Domain Asset Entity)
 * @note 严格禁止依赖任何外部存储 (SQLite/PostgreSQL)、传输协议 (HTTP) 或 JSON 框架
 */
typedef struct mf_asset {
    int64_t    id;
    int64_t    user_id;
    int64_t    category_id;
    char       name[128];
    char       account_no[64];
    char       asset_type[32];   /**< 资产类型代码: cash, bank, stock, fund, crypto, loan 等 */
    money_t    current_value;    /**< 当前估值/账户余额 */
    currency_t currency;         /**< 结算货币代码 */
    char       note[256];        /**< 备注信息 */
    quantity_t quantity;         /**< 投资持仓份额/数量 */
    money_t    cost_basis;       /**< 累计建仓与手续费成本基础 */
    price_t    net_value;        /**< 最新单位净值/成交单价 */
    char       symbol[64];       /**< 行情标的代码 (如 600519, AAPL) */
    char       quote_source[32]; /**< 行情源标识 (如 stock_cn, yahoo, binance) */
    char       created_at[64];
    char       updated_at[64];
} mf_asset_t;

/**
 * @brief 判断资产是否为投资类资产 (股票、基金、债券、加密货币)
 */
static inline bool
mf_asset_is_investment_type_str(const char* type)
{
    if (!type || type[0] == '\0') {
        return false;
    }
    return (strcmp(type, "stock") == 0 || strcmp(type, "fund") == 0 || strcmp(type, "bond") == 0 ||
            strcmp(type, "crypto") == 0);
}

/**
 * @brief 判断实体是否属于投资类资产
 */
static inline bool
mf_asset_is_investment(const mf_asset_t* asset)
{
    if (!asset) {
        return false;
    }
    return mf_asset_is_investment_type_str(asset->asset_type);
}

/**
 * @brief 判断是否为负债类资产 (信用卡、贷款、应付等)
 */
static inline bool
mf_asset_is_liability_type_str(const char* type)
{
    if (!type || type[0] == '\0') {
        return false;
    }
    return (strcmp(type, "credit_card") == 0 || strcmp(type, "loan") == 0 ||
            strcmp(type, "other_liability") == 0);
}

static inline bool
mf_asset_is_liability(const mf_asset_t* asset)
{
    if (!asset) {
        return false;
    }
    return mf_asset_is_liability_type_str(asset->asset_type);
}
