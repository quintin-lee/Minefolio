#pragma once

/**
 * @file balance.h
 * @brief 资产余额变动审计与投资持仓核算接口（Financial Core 驱动）
 *
 * 提供资产账户余额的原子更新及收支审计日志写入（balance_apply_delta_m / balance_apply_delta）、
 * 负债类资产符号自动反转判定（balance_direction）、投资类标的判定（is_investment_type）、
 * 以及股票/基金/债券/加密货币的持仓均价、份额与成本基础变动精确核算及回滚（apply_position_fc / rollback_position_fc）。
 */

#include "csilk/drivers/db.h"
#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "core/financial/money.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
#include <stdint.h>

/**
 * @brief 判断资产类型的资金流动方向系数
 * @param[in] asset_type 资产类别编码字符串（如 "cash", "credit_card", "loan" 等）
 * @return int 1: 正向资产账户；-1: 负债类账户
 */
int balance_direction(const char* asset_type);

/**
 * @brief 判断指定资产类别是否属于投资标的 (stock, fund, bond, crypto)
 * @param[in] atype 资产类别编码字符串
 * @return int 1: 投资类标的；0: 非投资类标的
 */
int is_investment_type(const char* atype);

/**
 * @brief 将投资标的单价折算到资产本币，独立保留 8 位小数精度（不走 2 位 currency_precision 截断）
 *
 * @param[in] price 源单价 (price_t)
 * @param[in] asset_cur 目标资产币种
 * @param[in] fx_rate_to_cny 交易发生时固化的源币种对 CNY 汇率（<=0 时回退当前牌价）
 * @return price_t 保留 8 位小数的折算后单价
 */
price_t balance_convert_price_fx(price_t price, currency_t asset_cur, double fx_rate_to_cny);

/**
 * @brief 对资产余额应用高精度金额增减变动，并同步写入资产流水审计日志
 *
 * @param[in] pool 数据库连接池指针
 * @param[in] asset_id 目标资产 ID
 * @param[in] user_id 用户 ID
 * @param[in] delta 变动金额 (money_t)
 * @param[in] source_type 触发业务源类型 ("daily_expense", "transaction", "transfer" 等)
 * @param[in] source_id 业务记录主键 ID
 * @param[in] note 审计日志备注说明
 * @return int 0: 成功；-1: 资产不存在/不属于该用户；-2: 数据库错误
 */
int balance_apply_delta_m(csilk_db_pool_t* pool,
                          int64_t          asset_id,
                          int64_t          user_id,
                          money_t          delta,
                          const char*      source_type,
                          int64_t          source_id,
                          const char*      note);

/**
 * @brief 使用交易发生时的历史汇率折算跨币种金额后应用余额变动
 *
 * 跨币种场景下，历史交易必须按交易创建时固化的 transactions.fx_rate_to_cny 折算，
 * 而非当前汇率，否则历史回放/回滚会随汇率波动产生漂移。
 *
 * @param[in] fx_rate_to_cny 交易发生时的「源币种 → CNY」汇率；<=0 或非有限值时
 *                           退化为 balance_apply_delta_m 的当前汇率路径。
 * @return int 0: 成功；-1: 资产不存在/不属于该用户；-2: 数据库错误
 */
int balance_apply_delta_m_fx(csilk_db_pool_t* pool,
                             int64_t          asset_id,
                             int64_t          user_id,
                             money_t          delta,
                             double           fx_rate_to_cny,
                             const char*      source_type,
                             int64_t          source_id,
                             const char*      note);

/**
 * @brief 兼容接口：对资产余额应用浮点增减变动（内部由 Financial Core 定点数精确执行）
 */
int balance_apply_delta(csilk_db_pool_t* pool,
                        int64_t          asset_id,
                        int64_t          user_id,
                        double           delta,
                        const char*      source_type,
                        int64_t          source_id,
                        const char*      note);

/**
 * @brief 对投资类资产应用买卖持仓变化（更新 quantity、cost_basis、net_value，精确无浮点误差）
 *
 * @note asset_id 由 user_id 限定（SQL 采用 WHERE id=? AND user_id=?），与
 *       balance_apply_delta_m 及账本重建路径的资产归属过滤保持一致，属纵深防御：
 *       若资产不属于该 user_id，则按“资产不存在”处理（持仓更新静默跳过，返回 0），
 *       杜绝跨用户串改 quantity/cost_basis/net_value。
 */
int apply_position_fc(csilk_db_pool_t* pool,
                      int64_t          asset_id,
                      int64_t          user_id,
                      const char*      type,
                      money_t          amount,
                      money_t          fee,
                      price_t          price,
                      quantity_t       qty,
                      double           fx_rate_to_cny,
                      money_t*         out_position_delta);

/**
 * @brief 兼容接口：更新投资资产持仓（内部由 Financial Core 定点数精确执行）
 */
int apply_position(csilk_db_pool_t* pool,
                   int64_t          asset_id,
                   int64_t          user_id,
                   const char*      type,
                   double           amount,
                   double           fee,
                   double           price,
                   double           qty,
                   double*          out_position_delta);

/**
 * @brief 回滚投资类资产的买卖持仓变化（基于交易事件流 + 检查点重放，取代手工逆向推算）
 *
 * 该函数不再手工反向推导 quantity/cost_basis，而是委托 ledger_rebuild_position_ex()
 * 从最近检查点（ledger_snapshots）开始重放 buy/sell 事件流并排除 exclude_tx_id，
 * 从而与写入路径（FIFO / 加权平均）共用同一套口径。
 *
 * @param[in] exclude_tx_id 需要在重放中排除的交易 ID（通常为被删除/修改的交易）
 * @param[out] out_position_delta 恒为 0：重建路径直接写入 assets.current_value，
 *                                 调用方不得再次应用余额 delta，否则会重复计入。
 *
 * @note 与 apply_position_fc 一致：asset_id 由 user_id 限定（WHERE id=? AND user_id=?），
 *       非归属资产按“不存在”处理（返回 -1，由调用方回滚事务），防止跨用户回滚串改。
 */
int rollback_position_fc(csilk_db_pool_t* pool,
                         int64_t          asset_id,
                         int64_t          user_id,
                         int64_t          exclude_tx_id,
                         money_t*         out_position_delta);

/**
 * @brief 兼容接口：回滚投资资产持仓变动（内部委托 ledger_rebuild_position_ex 重放）
 */
int rollback_position(csilk_db_pool_t* pool,
                      int64_t          asset_id,
                      int64_t          user_id,
                      int64_t          exclude_tx_id,
                      double*          out_position_delta);
