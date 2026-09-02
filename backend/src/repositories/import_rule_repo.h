#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

csilk_json_t* import_rule_list(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t* import_rule_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int64_t       import_rule_insert(csilk_db_pool_t* pool,
                                 int64_t          user_id,
                                 const char*      keyword,
                                 const char*      match_field,
                                 const char*      match_type,
                                 int64_t          category_id,
                                 const char*      target_type,
                                 int              priority,
                                 int              is_active);
int           import_rule_update(csilk_db_pool_t* pool,
                                 int64_t          user_id,
                                 int64_t          id,
                                 const char*      keyword,
                                 const char*      match_field,
                                 const char*      match_type,
                                 int64_t          category_id,
                                 const char*      target_type,
                                 int              priority,
                                 int              is_active);
int           import_rule_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
void          import_rule_seed_defaults(csilk_db_pool_t* pool, int64_t user_id);
