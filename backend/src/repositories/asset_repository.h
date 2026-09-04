#pragma once

/**
 * @file asset_repository.h
 * @brief 资产/标的定义与持仓状态存储数据访问层（纯数据映射，严禁业务计算与决策）
 */

#include "infrastructure/database/database.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t id;
    int64_t user_id;
    int64_t category_id;
    char    name[128];
    char    account_no[64];
    char    symbol[32];
    char    quote_source[32];
    char    currency[16];
    char    note[256];
    char    category_name[64];
    char    asset_type[32];
    double  current_value;
    double  quantity;
    double  cost_basis;
    double  net_value;
    char    last_sync_at[64];
    char    created_at[64];
    char    updated_at[64];
} asset_record_t;

int asset_repo_find_by_id(mf_db_t* db, int64_t user_id, int64_t id, asset_record_t* out_asset);

int64_t asset_repo_insert(mf_db_t*    db,
                          int64_t     user_id,
                          int64_t     category_id,
                          const char* name,
                          const char* account_no,
                          double      current_value,
                          const char* currency,
                          const char* note,
                          double      quantity,
                          double      cost_basis,
                          double      net_value,
                          const char* symbol,
                          const char* quote_source);

int asset_repo_update_basic(mf_db_t*    db,
                            int64_t     user_id,
                            int64_t     id,
                            const char* name,
                            const char* account_no,
                            double      current_value,
                            const char* currency,
                            const char* note,
                            const char* symbol,
                            const char* quote_source);

int asset_repo_update_position(mf_db_t* db,
                               int64_t  user_id,
                               int64_t  id,
                               double   quantity,
                               double   cost_basis,
                               double   net_value,
                               double   current_value);

int asset_repo_delete(mf_db_t* db, int64_t user_id, int64_t id);

#ifdef __cplusplus
}
#endif
