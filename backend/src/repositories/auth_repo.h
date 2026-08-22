#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
csilk_json_t* user_find_by_username(csilk_db_pool_t* pool, const char* username);
int64_t user_insert(csilk_db_pool_t* pool, const char* username, const char* password_hash);
csilk_json_t* user_get_by_id(csilk_db_pool_t* pool, int64_t user_id);
int user_update_password(csilk_db_pool_t* pool, int64_t user_id, const char* password_hash);
int user_update_token_version(csilk_db_pool_t* pool, int64_t user_id);
int user_count(csilk_db_pool_t* pool);
int system_is_initialized(csilk_db_pool_t* pool);
