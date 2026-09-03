#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "core/financial/currency.h"
#include "core/financial/money.h"

/**
 * @brief 周期性被动现金流计划实体 (Cashflow Schedule Entity)
 * @note 严格禁止依赖任何外部 DB、传输协议或 JSON 框架
 */
typedef struct mf_cashflow_schedule {
    int64_t id;
    int64_t user_id;
    int64_t source_asset_id; /**< 产生现金流的标的资产 (如理财、股票、房产) */
    int64_t target_asset_id; /**< 收益实际流入的目标资金账户 (如银行卡、活期钱包) */
    char    name[128];       /**< 计划名称 (如 "招行理财付息", "9月房屋租金") */
    char    flow_type[32];   /**< 类别 (如 "dividend", "interest", "rent", "salary") */
    char    frequency[32];   /**< 频次: "monthly", "quarterly", "semi_annual", "annual", "once" */
    char    start_date[32];  /**< 起始有效日期 YYYY-MM-DD */
    char    end_date[32];    /**< 截止日期 YYYY-MM-DD (可选) */
    money_t expected_amount; /**< 预期每期发生金额 */
    char    note[256];       /**< 备注 */
    char    status[32];      /**< 状态: "active", "paused", "completed" */
    char    created_at[64];
    char    updated_at[64];
} mf_cashflow_schedule_t;

/**
 * @brief 现金流日历预测/实际事件事实 (Calendar Event Fact)
 */
typedef struct mf_cashflow_event {
    int64_t    id; /**< 交易 ID 或排程 ID */
    int64_t    schedule_id;
    int64_t    source_asset_id;
    int64_t    target_asset_id;
    char       date[32]; /**< 事件发生日期 YYYY-MM-DD */
    char       name[128];
    char       source_asset_name[128];
    char       target_asset_name[128];
    char       flow_type[32];
    money_t    amount;
    currency_t currency;
    bool       is_actual;  /**< true: 真实历史入账交易; false: 未来排程预测 */
    char       status[32]; /**< "confirmed", "projected" */
} mf_cashflow_event_t;

/**
 * @brief 月度现金流日历汇总实体
 */
typedef struct mf_cashflow_calendar {
    int     year;
    int     month;
    char    year_month[32];
    money_t actual_total;
    money_t projected_total;
    money_t annual_projected_total;
} mf_cashflow_calendar_t;
