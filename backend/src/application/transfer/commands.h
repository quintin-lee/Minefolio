/**
 * @file commands.h
 * @brief 转账用例命令对象 (Application Transfer Commands)
 */

#pragma once

#include <stdint.h>

/**
 * @brief 创建转账命令
 */
typedef struct {
    int64_t     user_id;
    int64_t     from_asset_id;
    int64_t     to_asset_id;
    double      amount;
    const char* currency;
    const char* transfer_date;
    const char* note;
} create_transfer_cmd_t;
