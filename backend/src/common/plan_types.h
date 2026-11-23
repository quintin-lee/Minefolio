#pragma once

/**
 * @file plan_types.h
 * @brief 定投计划与被动现金流预测数据模型定义
 *
 * 定义定期定额投资计划（dca_plan_t）、单期定投执行记录（dca_execution_t）、
 * 以及周期性被动现金流（股息、利息、租金等）收支计划（cashflow_schedule_t）。
 */

#include <stdint.h>
#include <stdbool.h>

/**
 * @struct dca_plan_t
 * @brief 定投计划配置模型
 */
typedef struct {
    int64_t id;               /**< 定投计划主键 ID */
    int64_t user_id;          /**< 所属用户 ID */
    int64_t target_asset_id;  /**< 目标买入投资标的资产 ID */
    int64_t funding_asset_id; /**< 扣款扣费来源账户 ID */
    char    name[128];        /**< 定投计划名称 */
    char    frequency[32];    /**< 定投执行周期："weekly" 每周, "biweekly" 每双周, "monthly" 每月 */
    int     day_of_period;    /**< 周期内执行日期：每周对应 1-7（周一至周日），每月对应 1-31 */
    double  amount;           /**< 每期计划扣款定投金额 */
    double  target_profit_rate;   /**< 目标止盈收益率（如 0.15 表示 15%） */
    double  target_total_amount;  /**< 累计计划总定投金额上限 */
    int     target_total_periods; /**< 计划执行总期数上限 */
    char    status[32];     /**< 计划状态："active" 进行中, "paused" 已暂停, "completed" 已终止 */
    char    note[256];      /**< 备注信息 */
    char    created_at[32]; /**< 创建时间戳 */
    char    updated_at[32]; /**< 最后修改时间戳 */
} dca_plan_t;

/**
 * @struct dca_execution_t
 * @brief 单期定投执行历史记录模型
 */
typedef struct {
    int64_t id;                /**< 执行记录主键 ID */
    int64_t plan_id;           /**< 所属定投计划 ID */
    int64_t user_id;           /**< 所属用户 ID */
    char    period_date[16];   /**< 当期定投基准日期 ("YYYY-MM-DD") */
    double  planned_amount;    /**< 当期计划定投金额 */
    double  actual_amount;     /**< 实际成交总金额 */
    double  executed_price;    /**< 实际成交单价 */
    double  executed_quantity; /**< 实际成交买入份额 */
    int64_t transaction_id;    /**< 关联生成的实际交易流水 ID */
    char    status[32];     /**< 执行状态："pending" 待执行, "confirmed" 已完成, "skipped" 已跳过 */
    char    created_at[32]; /**< 记录生成时间戳 */
    char    updated_at[32]; /**< 记录状态变更时间戳 */
} dca_execution_t;

/**
 * @struct cashflow_schedule_t
 * @brief 周期性被动现金流收支计划模型
 */
typedef struct {
    int64_t id;              /**< 现金流计划主键 ID */
    int64_t user_id;         /**< 所属用户 ID */
    int64_t source_asset_id; /**< 产生现金流的底层资产 ID（如持有股票/房产） */
    int64_t target_asset_id; /**< 现金流结算入账的资金账户 ID */
    char    name[128];       /**< 现金流项目名称 */
    char    flow_type
        [32]; /**< 现金流类别："dividend" 股息分红, "interest" 债券利息/理财, "rent" 租金收入, "maturity" 本金到期 */
    char frequency
        [32]; /**< 发生频率："once" 一次性, "monthly" 按月, "quarterly" 季度, "semi_annual" 半年, "annual" 年度 */
    char   start_date[16];  /**< 现金流开始生效日期 ("YYYY-MM-DD") */
    char   end_date[16];    /**< 现金流终止日期 ("YYYY-MM-DD"，空字符串表示长期) */
    double expected_amount; /**< 预计每期产生现金流金额 */
    char   status[32];      /**< 状态："active" 启用, "completed" 已完成, "cancelled" 已作废 */
    char   note[256];       /**< 备注信息 */
    char   created_at[32];  /**< 创建时间戳 */
    char   updated_at[32];  /**< 更新时间戳 */
} cashflow_schedule_t;
