#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>

/**
 * @file dca_repo.h
 * @brief 自动定投计划 (DCA - Dollar-Cost Averaging) 与执行记录数据访问层接口
 *
 * 负责定期定额投资计划 (dca_plans) 以及每期定投执行记录 (dca_executions) 的 CRUD 数据持久化。
 * 提供计划状态控制（活跃/暂停/完成）、止盈目标跟踪、待执行期次生成与成交确认关联。
 */

/**
 * @brief 查询用户的所有定投计划列表
 *
 * 联查标的资产 (target_asset) 与扣款资金账户 (funding_asset)，
 * 并通过子查询统计已确认执行期数 (`executed_periods`) 与累计已投金额 (`total_invested_amount`)。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 包含定投计划及统计指标的 JSON 数组
 */
csilk_json_t* dca_plan_list(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 根据计划 ID 获取单个定投计划的完整详情与持仓状态
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（租户隔离验证）
 * @param id 定投计划 ID
 * @return csilk_json_t* 包含计划详情对象的 JSON 数组（长度为 1），未命中返回 NULL
 */
csilk_json_t* dca_plan_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 创建新的定投计划
 *
 * 初始状态自动设置为 `status='active'`。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param target_asset_id 目标买入资产 ID（如股票/基金）
 * @param funding_asset_id 扣款资金账户 ID（如银行卡/余额）
 * @param name 计划名称
 * @param frequency 周期频率（如 "weekly", "biweekly", "monthly"）
 * @param day_of_period 周期内的执行日期（如每月 15 号传入 15，每周一传入 1）
 * @param amount 每期计划定投金额
 * @param target_profit_rate 目标止盈收益率（如 0.15 表示 15%，0 表示为止盈）
 * @param target_total_amount 目标累计总投入金额上限（0 表示无上限）
 * @param target_total_periods 目标总定投期数（0 表示无限制）
 * @param note 备注
 * @return int64_t 成功返回新计划主键 ID，失败返回 -1
 */
int64_t dca_plan_create(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          target_asset_id,
                        int64_t          funding_asset_id,
                        const char*      name,
                        const char*      frequency,
                        int              day_of_period,
                        double           amount,
                        double           target_profit_rate,
                        double           target_total_amount,
                        int              target_total_periods,
                        const char*      note);

/**
 * @brief 更新指定的定投计划配置
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 计划 ID
 * @param target_asset_id 目标买入资产 ID
 * @param funding_asset_id 扣款资金账户 ID
 * @param name 计划名称
 * @param frequency 周期频率
 * @param day_of_period 周期执行日
 * @param amount 单期金额
 * @param target_profit_rate 止盈目标收益率
 * @param target_total_amount 累计总金额上限
 * @param target_total_periods 总期数限制
 * @param note 备注
 * @return int 成功返回 0，失败返回 -1
 */
int dca_plan_update(csilk_db_pool_t* pool,
                    int64_t          user_id,
                    int64_t          id,
                    int64_t          target_asset_id,
                    int64_t          funding_asset_id,
                    const char*      name,
                    const char*      frequency,
                    int              day_of_period,
                    double           amount,
                    double           target_profit_rate,
                    double           target_total_amount,
                    int              target_total_periods,
                    const char*      note);

/**
 * @brief 修改定投计划的状态（如 active, paused, completed）
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 计划 ID
 * @param status 新状态字符串
 * @return int 成功返回 0，失败返回 -1
 */
int dca_plan_set_status(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* status);

/**
 * @brief 删除定投计划
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 计划 ID
 * @return int 成功返回 0，失败返回 -1
 */
int dca_plan_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 查询系统内所有用户当前处于有效状态 (`status='active'`) 的定投计划
 *
 * 供后台定时调度引擎检索需要扫描生成或触发定投的计划列表。
 *
 * @param pool 数据库连接池指针
 * @return csilk_json_t* 包含全部活跃计划基本字段的 JSON 数组
 */
csilk_json_t* dca_plan_list_all_active(csilk_db_pool_t* pool);

/**
 * @brief 生成一条定投期次待执行记录
 *
 * 初始状态为 `status='pending'`。
 *
 * @param pool 数据库连接池指针
 * @param plan_id 所属定投计划 ID
 * @param user_id 用户 ID
 * @param period_date 本期计划执行日期 (YYYY-MM-DD)
 * @param planned_amount 本期计划买入金额
 * @return int64_t 成功返回新记录 ID，失败返回 -1
 */
int64_t dca_execution_create(csilk_db_pool_t* pool,
                             int64_t          plan_id,
                             int64_t          user_id,
                             const char*      period_date,
                             double           planned_amount);

/**
 * @brief 查询指定定投计划的所有历史执行记录
 *
 * 按期次日期逆序 (`period_date DESC, id DESC`) 排列。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param plan_id 定投计划 ID
 * @return csilk_json_t* 包含执行历史记录的 JSON 数组
 */
csilk_json_t* dca_execution_list_by_plan(csilk_db_pool_t* pool, int64_t user_id, int64_t plan_id);

/**
 * @brief 查询用户当前所有待确认/待执行的定投记录 (`status='pending'`)
 *
 * 联查计划与资产信息，供定投提醒与批量确认买入操作。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 待执行定投记录 JSON 数组
 */
csilk_json_t* dca_execution_list_pending(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 根据执行记录 ID 查询单条定投执行详情
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 执行记录 ID
 * @return csilk_json_t* 包含单条执行详情对象的 JSON 数组
 */
csilk_json_t* dca_execution_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 确认定投执行成功并关联交易流水
 *
 * 更新状态为 `status='confirmed'`，回填实际扣款金额、成交均价、成交份额及生成的交易流水 ID。
 *
 * @param pool 数据库连接池指针
 * @param id 执行记录 ID
 * @param actual_amount 实际扣款成交金额
 * @param executed_price 成交单价/净值
 * @param executed_quantity 实际买入份额
 * @param transaction_id 关联生成的交易流水 ID (`transactions.id`)
 * @return int 成功返回 0，失败返回 -1
 */
int dca_execution_update_confirmed(csilk_db_pool_t* pool,
                                   int64_t          id,
                                   double           actual_amount,
                                   double           executed_price,
                                   double           executed_quantity,
                                   int64_t          transaction_id);

/**
 * @brief 更新定投执行记录的状态（如 pending, skipped, failed）
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 执行记录 ID
 * @param status 目标状态字符串
 * @return int 成功返回 0，失败返回 -1
 */
int
dca_execution_update_status(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* status);
