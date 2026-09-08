/**
 * @file dtos.h
 * @brief 转账用例结果 DTO (Application Transfer DTOs)
 */

#pragma once

/**
 * @brief 通用用例结果
 */
typedef struct {
    int  code;
    char message[128];
} transfer_usecase_result_t;
