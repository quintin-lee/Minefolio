#pragma once

#include <stdint.h>
#include "core/financial/currency.h"
#include "core/financial/quantity.h"
#include "core/financial/price.h"
#include "core/financial/money.h"
#include "core/ledger/ledger_types.h"
#include "domain/asset/cost_basis.h"
#include "domain/asset/valuation.h"
#include "domain/asset/pnl.h"

/**
 * @brief 用户在指定账户对标的的持仓实体 (Position Entity)
 * 严格作为 Ledger 流水事件在时点上的物化投影 (Materialized Projection)
 */
typedef struct {
    int64_t         asset_id;   /**< 投资标的 ID */
    int64_t         account_id; /**< 关联账户 ID */
    currency_t      currency;   /**< 计价原生币种 */
    quantity_t      quantity;   /**< 当前持仓数量 */
    mf_cost_basis_t cost_basis; /**< 成本基础 */
    mf_valuation_t  valuation;  /**< 市场估值 */
    mf_pnl_t        pnl;        /**< 损益分析 */
} mf_position_t;

/**
 * @brief 核心派生规则：通过重放账本交易事件流生成最终 Position 事实 (Ledger -> Position)
 * @param asset_id 目标标的 ID
 * @param account_id 资金/证券账户 ID
 * @param native_currency 标的计价货币
 * @param tx_events 交易流水事件切片 (时间升序)
 * @param tx_count 事件条数
 * @param current_price 最新市场单价/净值
 * @param[out] out_position 派生生成的持仓实体指针
 * @return 0 成功, -1 失败 (如包含超卖等非法交易)
 */
int mf_position_derive_from_ledger(int64_t            asset_id,
                                   int64_t            account_id,
                                   currency_t         native_currency,
                                   const ledger_tx_t* tx_events,
                                   size_t             tx_count,
                                   price_t            current_price,
                                   mf_position_t*     out_position);
