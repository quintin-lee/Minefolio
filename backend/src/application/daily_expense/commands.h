/**
 * @file commands.h
 * @brief 日常收支用例命令对象 (Application Daily Expense Commands)
 */

#pragma once

#include <stdint.h>

/**
 * @brief 创建日常收支命令
 */
typedef struct {
    int64_t     user_id;
    int64_t     category_id;
    int64_t     asset_id;
    const char* expense_type;
    double      amount;
    const char* currency;
    const char* expense_date;
    const char* note;
    /* tags array will be passed separately */
} create_daily_expense_cmd_t;

/**
 * @brief 更新日常收支命令
 */
typedef struct {
    int64_t     user_id;
    int64_t     id;
    int64_t     category_id;
    int64_t     asset_id;
    const char* expense_type;
    double      amount;
    const char* currency;
    const char* expense_date;
    const char* note;
} update_daily_expense_cmd_t;
