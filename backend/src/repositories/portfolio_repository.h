#pragma once

/**
 * @file portfolio_repository.h
 * @brief 投资组合与持仓投影数据读取访问层（纯数据映射，严禁业务聚合与风险指标计算）
 */

#include "infrastructure/database/database.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t asset_id;
    char    name[128];
    char    symbol[32];
    char    currency[16];
    double  quantity;
    double  cost_basis;
    double  net_value;
    double  current_value;
} portfolio_position_row_t;

int portfolio_repo_fetch_positions(mf_db_t*                   db,
                                   int64_t                    user_id,
                                   portfolio_position_row_t** out_rows,
                                   int*                       out_count);

void portfolio_repo_free_positions(portfolio_position_row_t* rows);

#ifdef __cplusplus
}
#endif
