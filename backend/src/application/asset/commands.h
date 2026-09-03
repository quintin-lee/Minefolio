#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 创建资产命令 (Create Asset Command)
 */
typedef struct create_asset_cmd {
    int64_t     user_id;
    int64_t     ledger_id;
    int64_t     category_id;
    const char* name;
    const char* account_no;
    double      current_value;
    const char* currency;
    const char* note;
    double      quantity;
    double      cost_basis;
    double      net_value;
    const char* symbol;
    const char* quote_source;
} create_asset_cmd_t;

/**
 * @brief 更新资产命令 (Update Asset Command)
 */
typedef struct update_asset_cmd {
    int64_t     user_id;
    int64_t     id;
    const char* name;
    const char* account_no;
    double      current_value;
    const char* currency;
    const char* note;
    bool        has_quantity;
    double      quantity;
    bool        has_cost_basis;
    double      cost_basis;
    bool        has_net_value;
    double      net_value;
    const char* symbol;
    const char* quote_source;
} update_asset_cmd_t;

/**
 * @brief 删除资产命令 (Delete Asset Command)
 */
typedef struct delete_asset_cmd {
    int64_t user_id;
    int64_t id;
} delete_asset_cmd_t;

/**
 * @brief 查询资产过滤条件 (Query Asset Filter)
 */
typedef struct query_asset_filter {
    int64_t     user_id;
    const char* category_id;
    int64_t     page;
    int64_t     page_size;
} query_asset_filter_t;

/**
 * @brief 查询资产余额变更日志过滤条件 (Query Asset Balance Logs Filter)
 */
typedef struct query_asset_log_filter {
    int64_t     user_id;
    const char* asset_id;
    int64_t     page;
    int64_t     page_size;
} query_asset_log_filter_t;
