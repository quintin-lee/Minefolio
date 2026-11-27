/**
 * @file dtos.h
 * @brief 转账用例结果 DTO (Application Transfer DTOs)
 */

#pragma once

#include <stdint.h>

/**
 * @brief 通用用例结果
 */
typedef struct {
    int     code;
    char    message[128];
    int64_t id;
} transfer_usecase_result_t;
