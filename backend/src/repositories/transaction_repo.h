#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
csilk_json_t* tx_list(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          page,
                      int64_t          page_size,
                      const char*      asset_id,
                      const char*      category_id,
                      const char*      type,
                      const char*      source_type,
                      const char*      start_date,
                      const char*      end_date,
                      int64_t*         total);
csilk_json_t* tx_monthly(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);
int64_t       tx_insert(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          asset_id,
                        int64_t          linked_asset_id,
                        int64_t          category_id,
                        const char*      source_type,
                        const char*      transaction_type,
                        const char*      direction,
                        const char*      linked_direction,
                        double           amount,
                        double           price_per_unit,
                        double           quantity,
                        double           fee,
                        const char*      currency,
                        const char*      date,
                        const char*      note);
int           tx_update(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          id,
                        const char*      transaction_type,
                        const char*      direction,
                        const char*      linked_direction,
                        double           amount,
                        double           price_per_unit,
                        double           quantity,
                        const char*      currency,
                        const char*      date,
                        const char*      note,
                        int64_t          category_id,
                        const char*      source_type,
                        int64_t          linked_asset_id);
csilk_json_t* tx_get_old(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int           tx_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
csilk_json_t* tx_child_fee_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id);
int           tx_delete_fee_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id);

int           tx_asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);
