#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "core/financial/money.h"
#include "core/financial/quantity.h"
#include "core/financial/price.h"
#include "core/financial/rate.h"
#include "core/financial/percentage.h"

/**
 * @file ledger_types.h
 * @brief Ledger Engine 核心数据结构与金融事件定义
 */

/**
 * @brief 交易类型枚举
 */
typedef enum {
    LEDGER_TX_BUY = 1,      /**< 买入建仓/加仓 */
    LEDGER_TX_SELL,         /**< 卖出平仓/减仓 */
    LEDGER_TX_DEPOSIT,      /**< 资金存入/收入 */
    LEDGER_TX_WITHDRAW,     /**< 资金提取/支出 */
    LEDGER_TX_TRANSFER_IN,  /**< 转账转入 */
    LEDGER_TX_TRANSFER_OUT, /**< 转账转出 */
    LEDGER_TX_DIVIDEND,     /**< 分红派息 */
    LEDGER_TX_INTEREST,     /**< 利息收益 */
    LEDGER_TX_FEE,          /**< 独立手续费/规费 */
    LEDGER_TX_TAX,          /**< 税金支出 */
    LEDGER_TX_ADJUSTMENT,   /**< 账面核对调整 */
    LEDGER_TX_UNKNOWN = 99  /**< 未知类型 */
} ledger_tx_type_t;

/**
 * @brief 账本交易事件描述符
 */
typedef struct {
    int64_t          id;              /**< 交易记录主键 ID (新插入时为 0) */
    int64_t          user_id;         /**< 用户 ID */
    int64_t          asset_id;        /**< 目标标的/资金资产 ID */
    int64_t          linked_asset_id; /**< 关联资金账户 ID (可选，非关联交易为 0) */
    int64_t          category_id;     /**< 分类 ID */
    ledger_tx_type_t type;            /**< 交易类型 */
    const char*      type_str;        /**< 原始类型字符串 (如 "buy", "sell") */
    money_t          amount;          /**< 交易结算金额 */
    price_t          price;           /**< 成交单价/单位净值 */
    quantity_t       quantity;        /**< 成交份额数量 */
    money_t          fee;             /**< 交易手续费金额 */
    const char*      tx_date;         /**< 交易时间 (如 "2026-09-02 10:00:00") */
    const char*      note;            /**< 备注信息 */
    const char*      idempotency_key; /**< 幂等唯一键 (可选) */
    int64_t          parent_tx_id;    /**< 父级交易 ID (如手续费子记录) */
} ledger_tx_t;

/**
 * @brief 投资标的持仓状态派生物化数据 (Materialized Position State)
 */
typedef struct {
    int64_t    asset_id;       /**< 资产 ID */
    quantity_t quantity;       /**< 当前总持仓份额 */
    money_t    cost_basis;     /**< 当前总加权持仓成本 (含买入手续费) */
    price_t    net_value;      /**< 最新成交单价 / 单位净值 */
    money_t    current_value;  /**< 当前市场总市值 (quantity * net_value) */
    money_t    realized_pnl;   /**< 累计已实现盈亏 */
    money_t    unrealized_pnl; /**< 当前浮动盈亏 (current_value - cost_basis) */
} ledger_position_state_t;

/**
 * @brief 账户现金/负债余额状态派生物化数据 (Materialized Account State)
 */
typedef struct {
    int64_t asset_id; /**< 资产 ID */
    money_t balance;  /**< 当前物化现金余额（或负债欠款额） */
    int64_t tx_count; /**< 参与核算的历史交易与记账事件总数 */
} ledger_account_state_t;

/**
 * @brief 字符串交易类型转枚举
 */
ledger_tx_type_t ledger_tx_type_from_str(const char* str);

/**
 * @brief 枚举交易类型转标准字符串
 */
const char* ledger_tx_type_to_str(ledger_tx_type_t type);
