#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/* Returns 1 if both assets exist for user, 0 otherwise */
int transfer_asset_check(csilk_db_pool_t* pool, int64_t user_id, int64_t from_id, int64_t to_id);

/* Returns new transfer id, or 0 on failure */
int64_t transfer_insert(csilk_db_pool_t* pool, int64_t user_id, int64_t from_id, int64_t to_id,
                         double amount, const char* currency, const char* date, const char* note);
