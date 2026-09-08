/**
 * @file entity.h
 * @brief 定投计划领域实体定义 (Domain DCA Entity)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 定投计划实体
 */
typedef struct {
    int64_t id;                   /**< 主键 ID */
    int64_t user_id;              /**< 用户 ID */
    int64_t target_asset_id;      /**< 目标买入资产 ID */
    int64_t funding_asset_id;     /**< 扣款资金账户 ID */
    char    name[128];            /**< 计划名称 */
    char    frequency[16];        /**< 周期频率 (weekly/biweekly/monthly) */
    int     day_of_period;        /**< 周期内执行日 */
    double  amount;               /**< 每期定投金额 */
    double  target_profit_rate;   /**< 目标止盈收益率 */
    double  target_total_amount;  /**< 目标累计总投入 */
    int     target_total_periods; /**< 目标总期数 */
    char    status[16];           /**< 状态 (active/paused/completed) */
    char    note[256];            /**< 备注 */
    char    created_at[32];       /**< 创建时间 */
    char    updated_at[32];       /**< 更新时间 */
} mf_dca_plan_t;

/**
 * @brief 定投执行记录实体
 */
typedef struct {
    int64_t id;                /**< 主键 ID */
    int64_t plan_id;           /**< 所属计划 ID */
    int64_t user_id;           /**< 用户 ID */
    char    period_date[16];   /**< 本期执行日期 */
    double  planned_amount;    /**< 计划买入金额 */
    double  actual_amount;     /**< 实际成交金额 */
    double  executed_price;    /**< 成交均价 */
    double  executed_quantity; /**< 实际买入份额 */
    int64_t transaction_id;    /**< 关联交易流水 ID */
    char    status[16];        /**< 状态 (pending/confirmed/skipped/failed) */
    char    created_at[32];    /**< 创建时间 */
} mf_dca_execution_t;
