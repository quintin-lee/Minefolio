#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "core/financial/money.h"
#include "core/financial/quantity.h"
#include "core/financial/price.h"

/**
 * @brief 纯业务领域交易聚合根/实体 (Domain Entity)
 * @note 严格禁止依赖任何外部存储 (SQLite/PostgreSQL)、传输协议 (HTTP) 或 JSON 框架
 */
typedef struct mf_transaction {
    int64_t    id;
    int64_t    user_id;
    int64_t    asset_id;        /* 投资标的资产 ID (非投资类型为 0) */
    int64_t    account_id;      /* 资金账户 ID */
    int64_t    parent_tx_id;    /* 手续费子单关联的主交易 ID (无父单为 0) */
    char       type[32];        /* buy, sell, deposit, withdraw, fee, dividend 等 */
    quantity_t amount;          /* 交易数量/份额 (定点数) */
    price_t    price;           /* 单价 (定点数) */
    money_t    fee;             /* 手续费金额 (定点数) */
    char       fee_currency[8]; /* 手续费币种代码 */
    char       note[256];       /* 备注信息 */
    char       tx_time[64];     /* 交易发生时间 (ISO 8601 格式) */
    char       created_at[64];
    char       updated_at[64];
} mf_transaction_t;

/**
 * @brief 判断是否为投资类交易 (买入/卖出/分红等，需绑定标的资产)
 */
static inline bool
mf_tx_is_investment(const mf_transaction_t* tx)
{
    if (!tx) {
        return false;
    }
    return (tx->asset_id > 0);
}

/**
 * @brief 判断是否为手续费子单 (绑定了 parent_tx_id)
 */
static inline bool
mf_tx_is_fee_child(const mf_transaction_t* tx)
{
    if (!tx) {
        return false;
    }
    return (tx->parent_tx_id > 0);
}

/**
 * @brief 判断交易是否携带有效手续费
 */
static inline bool
mf_tx_has_fee(const mf_transaction_t* tx)
{
    if (!tx) {
        return false;
    }
    return money_is_positive(tx->fee);
}
