#pragma once

/**
 * @file balance.h
 * @brief 资产余额变动审计与投资持仓核算接口
 *
 * 提供资产账户余额的原子更新及收支审计日志写入（balance_apply_delta）、
 * 负债类资产符号自动反转判定（balance_direction）、投资类标的判定（is_investment_type）、
 * 以及股票/基金/债券/加密货币的持仓均价、份额与成本基础变动核算及回滚（apply_position / rollback_position）。
 */

#include "csilk/drivers/db.h"
#include <stdint.h>

/**
 * @brief 对资产余额应用增减变动，并同步写入资产流水审计日志
 *
 * 核心逻辑：
 * 1. 联合查询 assets 与 categories 表，获取资产的当前 current_value 及 asset_type。
 * 2. 校验资产所属权（user_id 匹配性）。
 * 3. 归一化变动方向：若资产类型为负债类（如 loan, credit_card），将 delta 乘以 -1 进行符号反转。
 * 4. 原子执行 SQL `UPDATE assets SET current_value = current_value + ?` 避免并发读改写丢失更新。
 * 5. 获取更新后的 balance_after 快照。
 * 6. 向 `asset_balance_logs` 表插入一条包含变动量、期末余额、来源类型及备注的审计日志。
 *
 * @param[in] pool 数据库连接池指针，不可为 NULL
 * @param[in] asset_id 目标资产 ID
 * @param[in] user_id 当前操作用户 ID（用于鉴权与审计）
 * @param[in] delta 业务层面的变动金额（收入/入金为正数，支出/出金/费用为负数）
 * @param[in] source_type 触发余额变动的业务源类型（如 "daily_expense" 或 "transaction"）
 * @param[in] source_id 关联的业务记录主键 ID
 * @param[in] note 审计日志冗余备注说明，可为 NULL
 *
 * @return int 状态码
 * @retval 0 成功应用变动并记录审计日志
 * @retval -1 资产不存在或该资产不属于该用户
 * @retval -2 数据库操作或 SQL 执行失败
 *
 * @note 线程安全性：SQL 原子更新，依靠底层数据库事务与行级锁保证安全。
 */
int balance_apply_delta(csilk_db_pool_t* pool,
                        int64_t          asset_id,
                        int64_t          user_id,
                        double           delta,
                        const char*      source_type,
                        int64_t          source_id,
                        const char*      note);

/**
 * @brief 判断资产类型的资金流动方向系数
 *
 * 针对负债类账户（如 loan 贷款、credit_card 信用卡、other_liability 其它负债），
 * 借入/消费会导致负债增加但净资产减少，其数值方向与资产账户相反。
 *
 * @param[in] asset_type 资产类别编码字符串（如 "cash", "credit_card", "loan" 等）
 *
 * @return int 方向系数
 * @retval 1 普通资产账户（正向）
 * @retval -1 负债类账户（反向）
 *
 * @note 线程安全性：纯字符串比对，线程安全。
 */
int balance_direction(const char* asset_type);

/**
 * @brief 判断指定资产类别是否属于投资标的
 *
 * 支持的投资类别包括：stock (股票), fund (基金), bond (债券), crypto (加密货币)。
 *
 * @param[in] atype 资产类别编码字符串
 *
 * @return int 判定结果
 * @retval 1 属于投资类标的
 * @retval 0 非投资类标的（如现金、银行账户、房产等）
 *
 * @note 线程安全性：线程安全。
 */
int is_investment_type(const char* atype);

/**
 * @brief 对投资类资产应用买卖持仓变化（更新 quantity、cost_basis、net_value）
 *
 * 运算规则：
 * 1. 仅对投资类资产生效；非投资类资产直接返回 0 且不修改数据。
 * 2. 买入 ("buy")：
 *    - new_quantity = old_quantity + qty
 *    - new_cost_basis = old_cost_basis + amount + fee（手续费计入总持仓成本）
 *    - new_net_value = price（以最新成交价更新单位净值）
 * 3. 卖出 ("sell")：
 *    - 校验持仓是否充足（old_quantity >= qty），不足则返回 -1 报错。
 *    - 按加权平均成本比例扣减：avg_cost = old_cost_basis / old_quantity
 *    - new_quantity = old_quantity - qty
 *    - new_cost_basis = old_cost_basis - qty * avg_cost
 *    - new_net_value = old_net_value
 * 4. 计算持仓总市值变动量 delta = (new_quantity * new_net_value) - old_current_value，并通过 out_position_delta 返回。
 *
 * @param[in] pool 数据库连接池指针
 * @param[in] asset_id 目标投资标的资产 ID
 * @param[in] type 交易类型（"buy" 买入 或 "sell" 卖出）
 * @param[in] amount 交易成交金额
 * @param[in] fee 交易手续费
 * @param[in] price 成交单价
 * @param[in] qty 成交数量/份额
 * @param[out] out_position_delta 接收计算出的持仓市值变化量增量指针，可为 NULL
 *
 * @return int 状态码
 * @retval 0 成功应用持仓变动（或资产非投资类直接跳过）
 * @retval -1 卖出份额超出当前可用持仓
 *
 * @note 线程安全性：线程安全。
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
 * @brief 回滚投资类资产的买卖持仓变化（用于修改或删除投资交易）
 *
 * 执行 apply_position 的完全逆运算：
 * - 回滚买入：扣减 quantity 与包含手续费的 cost_basis。
 * - 回滚卖出：恢复扣除的 quantity 与按平均成本计算的 cost_basis。
 *
 * @param[in] pool 数据库连接池指针
 * @param[in] asset_id 目标资产 ID
 * @param[in] type 原始交易类型（"buy" 或 "sell"）
 * @param[in] amount 原始交易金额
 * @param[in] fee 原始手续费
 * @param[in] price 原始成交单价
 * @param[in] qty 原始成交份额
 * @param[out] out_position_delta 接收回滚后市值变化量的指针，可为 NULL
 *
 * @return int 状态码
 * @retval 0 成功回滚
 * @retval -1 回滚失败
 *
 * @note 线程安全性：线程安全。
 */
int rollback_position(csilk_db_pool_t* pool,
                      int64_t          asset_id,
                      const char*      type,
                      double           amount,
                      double           fee,
                      double           price,
                      double           qty,
                      double*          out_position_delta);
