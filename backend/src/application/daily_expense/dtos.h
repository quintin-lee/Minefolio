/**
 * @file dtos.h
 * @brief 日常收支用例结果 DTO (Application Daily Expense DTOs)
 */

#pragma once
#include "core/financial/money.h"

/**
 * @brief 通用用例结果
 */
typedef struct {
    int  code;
    char message[128];
} daily_expense_usecase_result_t;

/**
 * @brief 月度聚合结果（金额以 CNY 基准的 money_t 精确存储，输出前转 double）
 */
typedef struct {
    int     code;
    char    message[128];
    money_t total_income;
    money_t total_expense;
    money_t balance;
} daily_expense_monthly_result_t;
