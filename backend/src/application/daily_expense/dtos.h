/**
 * @file dtos.h
 * @brief 日常收支用例结果 DTO (Application Daily Expense DTOs)
 */

#pragma once

/**
 * @brief 通用用例结果
 */
typedef struct {
    int  code;
    char message[128];
} daily_expense_usecase_result_t;

/**
 * @brief 月度聚合结果
 */
typedef struct {
    int    code;
    char   message[128];
    double total_income;
    double total_expense;
    double balance;
} daily_expense_monthly_result_t;
