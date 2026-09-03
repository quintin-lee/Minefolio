#pragma once

#include <stdint.h>

/**
 * @brief 创建交易命令 (Create Transaction Command)
 */
typedef struct create_tx_cmd {
    int64_t     user_id;
    int64_t     asset_id;
    int64_t     linked_asset_id;
    int64_t     category_id;
    const char* type;
    double      amount;
    double      price;
    double      quantity;
    double      fee;
    const char* currency;
    const char* note;
    const char* date;
    const char* source_type;
} create_tx_cmd_t;

/**
 * @brief 更新交易命令 (Update Transaction Command)
 */
typedef struct update_tx_cmd {
    int64_t     user_id;
    int64_t     tx_id;
    int64_t     asset_id;
    int64_t     linked_asset_id;
    int64_t     category_id;
    const char* type;
    double      amount;
    double      price;
    double      quantity;
    double      fee;
    const char* currency;
    const char* note;
    const char* date;
} update_tx_cmd_t;

/**
 * @brief 删除交易命令 (Delete Transaction Command)
 */
typedef struct delete_tx_cmd {
    int64_t user_id;
    int64_t tx_id;
} delete_tx_cmd_t;

/**
 * @brief 查询交易过滤条件 (Query Transaction Filter)
 */
typedef struct query_tx_filter {
    int64_t     user_id;
    int64_t     asset_id;
    int64_t     category_id;
    const char* type;
    const char* start_date;
    const char* end_date;
    int64_t     page;
    int64_t     page_size;
} query_tx_filter_t;
