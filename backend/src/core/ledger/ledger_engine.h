#pragma once
#include "csilk/csilk.h"
#include "core/ledger/ledger_types.h"

/**
 * @file ledger_engine.h
 * @brief Minefolio 统一账本核心状态机与事件溯源引擎接口
 */

/* =========================================================================
 * 1. 纯数学计算算子（Pure Mathematical Functions - Zero Side Effects）
 * ========================================================================= */

/**
 * @brief 计算加仓/买入后的新持仓份额与加权成本基础
 *
 * 公式：
 * - new_quantity = prev_quantity + buy_quantity
 * - new_cost_basis = prev_cost_basis + buy_amount + buy_fee
 *
 * @param prev_qty   加仓前持仓份额
 * @param prev_cost  加仓前总成本
 * @param buy_qty    本次买入份额
 * @param buy_price  本次买入成交价
 * @param buy_fee    本次买入手续费
 * @param out_qty    输出加仓后总份额
 * @param out_cost   输出加仓后总成本
 * @return 0 成功；<0 错误
 */
int ledger_calc_buy_position(quantity_t  prev_qty,
                             money_t     prev_cost,
                             quantity_t  buy_qty,
                             price_t     buy_price,
                             money_t     buy_fee,
                             quantity_t* out_qty,
                             money_t*    out_cost);

/**
 * @brief 计算减仓/卖出后的新持仓份额、成本扣减额及已实现盈亏
 *
 * 公式：
 * - cost_reduction = (sell_qty / prev_qty) * prev_cost
 * - new_quantity = prev_qty - sell_qty
 * - new_cost_basis = prev_cost - cost_reduction
 * - realized_pnl = (sell_amount - sell_fee) - cost_reduction
 *
 * @param prev_qty            减仓前总份额
 * @param prev_cost           减仓前总成本
 * @param sell_qty            本次卖出份额
 * @param sell_price          本次卖出成交单价
 * @param sell_fee            本次卖出手续费
 * @param out_qty             输出减仓后剩余份额
 * @param out_cost            输出减仓后剩余成本
 * @param out_cost_reduction  输出扣减的持仓成本
 * @param out_realized_pnl    输出本次卖出实现的净盈亏额
 * @return 0 成功；<0 错误（如持仓不足）
 */
int ledger_calc_sell_position(quantity_t  prev_qty,
                              money_t     prev_cost,
                              quantity_t  sell_qty,
                              price_t     sell_price,
                              money_t     sell_fee,
                              quantity_t* out_qty,
                              money_t*    out_cost,
                              money_t*    out_cost_reduction,
                              money_t*    out_realized_pnl);

/**
 * @brief 计算浮动盈亏 (Unrealized PnL = current_market_value - cost_basis)
 */
int ledger_calc_unrealized_pnl(quantity_t qty,
                               price_t    net_val,
                               money_t    cost_basis,
                               money_t*   out_unrealized_pnl);

/* =========================================================================
 * 2. 数据库事务操作算子 (Database Transaction Operators)
 * ========================================================================= */

/**
 * @brief 应用交易事实到账本引擎（原子更新标的持仓/余额、资金账户、记录手续费子记录并生成审计日志）
 * 必须处于活跃的数据库事务中（BEGIN TRANSACTION ... COMMIT/ROLLBACK）。
 *
 * @param pool 数据库连接池
 * @param tx   交易事件描述符（若 tx->id == 0，将自动执行 INSERT 并填充 tx->id）
 * @return 0 成功；< 0 失败
 */
int ledger_apply_tx(csilk_db_pool_t* pool, ledger_tx_t* tx);

/**
 * @brief 逆向回滚已生效的交易事实（回滚持仓、反转资金账户变动、回滚并清理所有关联手续费子项）
 * 必须处于活跃的数据库事务中。
 *
 * @param pool    数据库连接池
 * @param user_id 用户 ID
 * @param tx_id   待回滚的交易主键 ID
 * @return 0 成功；< 0 失败
 */
int ledger_reverse_tx(csilk_db_pool_t* pool, int64_t user_id, int64_t tx_id);

/**
 * @brief 记录日常收支记账事件并更新对应资产现金/负债余额
 */
int ledger_apply_expense(csilk_db_pool_t* pool,
                         int64_t          user_id,
                         int64_t          asset_id,
                         money_t          amount,
                         int              is_income,
                         int64_t          expense_id,
                         const char*      note);

/**
 * @brief 逆向回滚日常收支记账事件
 */
int ledger_reverse_expense(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           int64_t          asset_id,
                           money_t          amount,
                           int              is_income,
                           int64_t          expense_id,
                           const char*      note);

/**
 * @brief 记录资产间调拨转账事件并原子更新双方账户余额
 */
int ledger_apply_transfer(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          int64_t          from_asset_id,
                          int64_t          to_asset_id,
                          money_t          amount,
                          int64_t          transfer_id,
                          const char*      note);

/**
 * @brief 逆向回滚资产间调拨转账事件
 */
int ledger_reverse_transfer(csilk_db_pool_t* pool,
                            int64_t          user_id,
                            int64_t          from_asset_id,
                            int64_t          to_asset_id,
                            money_t          amount,
                            int64_t          transfer_id,
                            const char*      note);

/* =========================================================================
 * 3. 状态重算与事件溯源重建 (State Rebuild / Event Sourcing Engine)
 * ========================================================================= */

/**
 * @brief 从零按时间戳顺序重放指定投资资产的所有交易，重新推导并物化其持仓份额、加权成本基础与市值
 *
 * @param pool      数据库连接池
 * @param user_id   用户 ID
 * @param asset_id  目标投资资产 ID
 * @param out_state 输出重算后的最终持仓状态（可选，可传 NULL）
 * @return 0 成功；< 0 失败
 */
int ledger_rebuild_position(csilk_db_pool_t*         pool,
                            int64_t                  user_id,
                            int64_t                  asset_id,
                            ledger_position_state_t* out_state);

/**
 * @brief 从零按时间戳顺序重放指定资金/负债账户的所有交易、收支及转账，重新推导并物化其当前现金/负债余额
 *
 * @param pool      数据库连接池
 * @param user_id   用户 ID
 * @param asset_id  目标资金资产 ID
 * @param out_state 输出重算后的最终账户状态（可选，可传 NULL）
 * @return 0 成功；< 0 失败
 */
int ledger_rebuild_account(csilk_db_pool_t*        pool,
                           int64_t                 user_id,
                           int64_t                 asset_id,
                           ledger_account_state_t* out_state);

/**
 * @brief 全局重算用户的所有资产账户与投资标的状态（保证：original state == rebuild state）
 *
 * @param pool    数据库连接池
 * @param user_id 用户 ID
 * @return 0 成功；< 0 失败
 */
int ledger_rebuild_portfolio(csilk_db_pool_t* pool, int64_t user_id);
