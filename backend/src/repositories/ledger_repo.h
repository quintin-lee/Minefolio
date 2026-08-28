#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>
#include <stdbool.h>

csilk_json_t* ledger_list_by_user(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t* ledger_get(csilk_db_pool_t* pool, int64_t ledger_id);
int64_t ledger_get_default(csilk_db_pool_t* pool, int64_t user_id);
int64_t ledger_create(csilk_db_pool_t* pool, int64_t owner_id, const char* name,
                      const char* description, const char* currency, const char* icon,
                      const char* color, bool is_default);
int ledger_update(csilk_db_pool_t* pool, int64_t ledger_id, const char* name,
                  const char* description, const char* currency, const char* icon,
                  const char* color);
int ledger_delete(csilk_db_pool_t* pool, int64_t ledger_id);

csilk_json_t* ledger_member_list(csilk_db_pool_t* pool, int64_t ledger_id);
const char* ledger_get_user_role(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, char* out_role, size_t out_len);
int ledger_member_add(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, const char* role);
int ledger_member_update_role(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, const char* new_role);
int ledger_member_remove(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id);

int ledger_update_invite_code(csilk_db_pool_t* pool, int64_t ledger_id, const char* invite_code, const char* expires_at);
csilk_json_t* ledger_find_by_invite_code(csilk_db_pool_t* pool, const char* invite_code);
