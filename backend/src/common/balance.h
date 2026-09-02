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
 */
int apply_position_fc(csilk_db_pool_t* pool,
                      int64_t          asset_id,
                      const char*      type,
                      money_t          amount,
                      money_t          fee,
                      price_t          price,
                      quantity_t       qty,
                      money_t*         out_position_delta);

/**
 * @brief 兼容接口：更新投资资产持仓（内部由 Financial Core 定点数精确执行）
 */
int apply_position(csilk_db_pool_t* pool,
                   int64_t          asset_id,
                   const char*      type,
                   double           amount,
                   double           fee,
                   double           price,
                   double           qty,
                   double*          out_position_delta);

/**
 * @brief 回滚投资类资产的买卖持仓变化（用于修改或删除投资交易，高精度定点执行）
 */
int rollback_position_fc(csilk_db_pool_t* pool,
                         int64_t          asset_id,
                         const char*      type,
                         money_t          amount,
                         money_t          fee,
                         price_t          price,
                         quantity_t       qty,
                         money_t*         out_position_delta);

/**
 * @brief 兼容接口：回滚投资资产持仓变动（内部由 Financial Core 定点数精确执行）
 */
int rollback_position(csilk_db_pool_t* pool,
                      int64_t          asset_id,
                      const char*      type,
                      double           amount,
                      double           fee,
                      double           price,
                      double           qty,
                      double*          out_position_delta);
