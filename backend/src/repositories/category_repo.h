#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
csilk_json_t* category_list(csilk_db_pool_t* pool, int64_t user_id, const char* type);
csilk_json_t* category_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int64_t       category_insert(csilk_db_pool_t* pool,
                              int64_t          user_id,
                              const char*      name,
                              int64_t          parent_id,
                              const char*      type,
                              const char*      asset_type,
                              const char*      currency,
                              const char*      icon,
                              int              sort_order);
int           category_update(csilk_db_pool_t* pool,
                              int64_t          user_id,
                              int64_t          id,
                              const char*      name,
                              const char*      type,
                              const char*      asset_type,
                              const char*      currency,
                              const char*      icon,
                              int              sort_order);
int           category_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
csilk_json_t* category_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_id);
int64_t       category_find_or_create(csilk_db_pool_t* pool,
                                      int64_t          user_id,
                                      const char*      name,
                                      int64_t          parent_id,
                                      const char*      type,
                                      const char*      asset_type,
                                      const char*      icon,
                                      int              sort_order);
int           category_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
