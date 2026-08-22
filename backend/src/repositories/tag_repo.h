#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

csilk_json_t* tag_list(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t* tag_suggestions(csilk_db_pool_t* pool, int64_t user_id, const char* prefix);
int64_t tag_insert(csilk_db_pool_t* pool, int64_t user_id, const char* name, const char* color);
int tag_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* name, const char* color);
int tag_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
