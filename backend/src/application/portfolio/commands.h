#pragma once

#include <stdint.h>

/**
 * @brief 持仓与组合报表查询命令
 */
typedef struct query_portfolio_cmd {
    int64_t     user_id;
    const char* base_currency; /**< 基准货币代码，默认 CNY */
} query_portfolio_cmd_t;
