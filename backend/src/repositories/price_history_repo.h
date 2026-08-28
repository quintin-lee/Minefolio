#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

int price_history_record(csilk_db_pool_t* pool,
                         int64_t          asset_id,
                         const char*      price_date,
                         double           price,
                         const char*      currency);
csilk_json_t*
price_history_list_by_asset(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, int limit);
