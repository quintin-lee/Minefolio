/**
 * @file commands.h
 * @brief 定投计划用例命令对象 (Application DCA Commands)
 */

#pragma once

#include <stdint.h>

/**
 * @brief 创建定投计划命令
 */
typedef struct {
    int64_t     user_id;
    int64_t     target_asset_id;
    int64_t     funding_asset_id;
    const char* name;
    const char* frequency;
    int         day_of_period;
    double      amount;
    double      target_profit_rate;
    double      target_total_amount;
    int         target_total_periods;
    const char* note;
} create_dca_plan_cmd_t;

/**
 * @brief 更新定投计划命令
 */
typedef struct {
    int64_t     user_id;
    int64_t     id;
    int64_t     target_asset_id;
    int64_t     funding_asset_id;
    const char* name;
    const char* frequency;
    int         day_of_period;
    double      amount;
    double      target_profit_rate;
    double      target_total_amount;
    int         target_total_periods;
    const char* note;
} update_dca_plan_cmd_t;

/**
 * @brief 确认定投执行命令
 */
typedef struct {
    int64_t user_id;
    int64_t exec_id;
    double  actual_amount;
    double  executed_price;
} confirm_dca_execution_cmd_t;
