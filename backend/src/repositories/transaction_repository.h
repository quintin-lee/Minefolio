#pragma once

/**
 * @file transaction_repository.h
 * @brief 交易明细与层级关联数据访问层（纯数据映射，严禁业务规则、权限决策、余额计算）
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
    int64_t asset_id;
    int64_t linked_asset_id;
    int64_t category_id;
    int64_t parent_tx_id;
    char    transaction_type[32];
    double  amount;
    double  fee;
    double  price;
    double  quantity;
    char    direction[16];
    char    linked_direction[16];
    char    transaction_time[64];
    char    note[256];
    char    created_at[64];
} tx_record_t;

int tx_repo_find_by_id(mf_db_t* db, int64_t user_id, int64_t id, tx_record_t* out_tx);

int64_t tx_repo_insert(mf_db_t*    db,
                       int64_t     user_id,
                       int64_t     asset_id,
                       int64_t     linked_asset_id,
                       int64_t     category_id,
                       const char* transaction_type,
                       double      amount,
                       double      fee,
                       double      price,
                       double      quantity,
                       const char* direction,
                       const char* linked_direction,
                       const char* transaction_time,
                       const char* note,
                       int64_t     parent_tx_id);

int tx_repo_delete(mf_db_t* db, int64_t user_id, int64_t id);

int tx_repo_delete_children_by_parent(mf_db_t* db, int64_t user_id, int64_t parent_tx_id);

#ifdef __cplusplus
}
#endif
