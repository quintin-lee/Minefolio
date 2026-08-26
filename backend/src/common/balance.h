#pragma once
#include "csilk/drivers/db.h"
#include <stdint.h>

/**
 * @brief 对资产余额应用增减，并写入审计日志。
 *
 * delta 为业务方向金额（收入/入金为正，支出/出金为负），函数内部根据资产
 * 类型（负债方向反转）归一化后更新 current_value，并记录 balance_after 快照。
 *
 * @param pool        数据库连接池
 * @param asset_id    目标资产 id
 * @param user_id     操作者（审计 + 归属校验）
 * @param delta       业务方向金额（正=增加余额，负=减少余额）
 * @param source_type "daily_expense" 或 "transaction"
 * @param source_id   对应主记录 id
 * @param note        冗余描述（可为 NULL）
 * @return 0 成功；-1 资产不存在或不属于该用户；-2 数据库错误
 */
int balance_apply_delta(csilk_db_pool_t* pool,
                        int64_t          asset_id,
                        int64_t          user_id,
                        double           delta,
                        const char*      source_type,
                        int64_t          source_id,
                        const char*      note);

/** @brief 判断资产类型是否为负债（方向反转）。1=普通资产，-1=负债。 */
int balance_direction(const char* asset_type);

/** @brief 判断资产类型是否属于投资类（stock/fund/bond/crypto）。 */
int is_investment_type(const char* atype);

/**
 * @brief 对投资类资产应用买卖持仓变化（quantity/cost_basis/net_value）。
 *
 * 仅在资产分类为投资类时生效；买入时 cost_basis 含手续费，卖出时按均值成本
 * 比例扣减。通过 out_position_delta 返回当前市值变化量，供调用方同步余额。
 *
 * @param pool              数据库连接池
 * @param asset_id          目标投资资产 id
 * @param type              "buy" 或 "sell"
 * @param amount            成交金额
 * @param fee               手续费
 * @param price             成交单价（买入时成为新 net_value）
 * @param qty               成交份额
 * @param out_position_delta 输出：持仓市值变化量（可为 NULL）
 * @return 0 成功或无变化；-1 卖出份额不足；资产非投资类时返回 0 且不修改
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
 * @brief 回滚投资类资产的买卖持仓变化（用于编辑/删除投资交易）。
 */
int rollback_position(csilk_db_pool_t* pool,
                      int64_t          asset_id,
                      const char*      type,
                      double           amount,
                      double           fee,
                      double           price,
                      double           qty,
                      double*          out_position_delta);
