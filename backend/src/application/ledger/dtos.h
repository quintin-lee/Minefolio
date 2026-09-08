/**
 * @file dtos.h
 * @brief 账本用例结果 DTO (Application Ledger DTOs)
 */

#pragma once

#include <stdint.h>

/**
 * @brief 通用用例结果
 */
typedef struct {
    int  code; /**< 0=成功, 1002=参数错误, 1003=未找到, 1004=禁止 */
    char message[128];
} ledger_usecase_result_t;

/**
 * @brief 邀请码结果
 */
typedef struct {
    int  code;
    char message[128];
    char invite_code[8];
    char expires_at[32];
} ledger_invite_result_t;
