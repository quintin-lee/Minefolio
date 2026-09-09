#pragma once

#include "domain/transaction/repository.h"
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>

/**
 * @file transaction_repo_impl.h
 * @brief 基础设施层交易仓储实现头文件
 * @note 实现 domain/transaction/repository.h 声明的所有纯 C 仓储契约
 */

/* Legacy function signatures for backward compatibility */
csilk_json_t* tx_list(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          page,
                      int64_t          page_size,
                      const char*      asset_id,
                      const char*      category_id,
                      const char*      type,
                      const char*      from_date,
                      const char*      to_date,
                      const char*      keyword,
                      int64_t*         total);
csilk_json_t* tx_monthly(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);
int64_t       tx_insert(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          asset_id,
                        int64_t          linked_asset_id,
                        int64_t          parent_tx_id,
                        const char*      transaction_type,
                        const char*      sub_type,
                        const char*      direction_in,
                        const char*      direction_out,
                        double           amount,
                        double           price_per_unit,
                        double           quantity,
                        double           fee,
                        const char*      currency,
                        const char*      transaction_date,
                        const char*      note);
int           tx_update(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          id,
                        int64_t          asset_id,
                        const char*      transaction_type,
                        const char*      sub_type,
                        const char*      direction_in,
                        const char*      direction_out,
                        double           amount,
                        double           price_per_unit,
                        double           quantity,
                        double           fee,
                        const char*      currency,
                        const char*      transaction_date,
                        const char*      note);
csilk_json_t* tx_get_old(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int           tx_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
csilk_json_t* tx_child_fee_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id);
int           tx_delete_fee_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id);
int           tx_asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);
