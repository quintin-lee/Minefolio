#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

csilk_json_t* asset_list(csilk_db_pool_t* pool,
                         int64_t          user_id,
                         int64_t          page,
                         int64_t          page_size,
                         const char*      category_id,
                         int64_t*         total);
csilk_json_t* asset_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int64_t       asset_insert(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           int64_t          category_id,
                           const char*      name,
                           const char*      account_no,
                           double           current_value,
                           const char*      currency,
                           const char*      note,
                           double           quantity,
                           double           cost_basis,
                           double           net_value,
                           const char*      symbol,
                           const char*      quote_source);
int           asset_update_basic(csilk_db_pool_t* pool,
                                 int64_t          user_id,
                                 int64_t          id,
                                 const char*      name,
                                 const char*      account_no,
                                 double           current_value,
                                 const char*      currency,
                                 const char*      note,
                                 const char*      symbol,
                                 const char*      quote_source);
int           asset_update_position(csilk_db_pool_t* pool,
                                    int64_t          user_id,
                                    int64_t          id,
                                    double           net_value,
                                    double           quantity,
                                    double           cost_basis);
int           asset_update_market_quote(csilk_db_pool_t* pool,
                                        int64_t          user_id,
                                        int64_t          asset_id,
                                        double           new_net_value);
csilk_json_t* asset_list_for_sync(csilk_db_pool_t* pool, int64_t user_id);
int           asset_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int           asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
char*         asset_get_category_type(csilk_db_pool_t* pool, int64_t user_id, int64_t category_id);
csilk_json_t* asset_transactions(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);
