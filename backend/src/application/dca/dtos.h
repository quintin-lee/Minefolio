/**
 * @file dtos.h
 * @brief 定投计划用例结果 DTO (Application DCA DTOs)
 */

#pragma once

/**
 * @brief 通用用例结果
 */
typedef struct {
    int  code;
    char message[128];
} dca_usecase_result_t;

/**
 * @brief 确认执行结果
 */
typedef struct {
    int     code;
    char    message[128];
    int64_t transaction_id;
    double  actual_amount;
    double  executed_price;
    double  executed_quantity;
} dca_confirm_result_t;
