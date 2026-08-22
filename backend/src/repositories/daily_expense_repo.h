#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
csilk_json_t* de_list(csilk_db_pool_t* pool, int64_t user_id, int64_t page, int64_t page_size,
                       const char* expense_type, const char* category_id, const char* tag_ids,
                       const char* start_date, const char* end_date, int64_t* total);
csilk_json_t* de_monthly_totals(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);
csilk_json_t* de_monthly_by_category(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);
csilk_json_t* de_monthly_by_tag(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);
csilk_json_t* de_monthly_daily(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);
int64_t de_insert(csilk_db_pool_t* pool, int64_t user_id, int64_t category_id, int64_t asset_id,
                   const char* expense_type, double amount, const char* currency, const char* date, const char* note);
csilk_json_t* de_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int de_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, int64_t category_id, int64_t asset_id,
              const char* expense_type, double amount, const char* currency, const char* date, const char* note);
int de_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int de_tag_insert(csilk_db_pool_t* pool, int64_t expense_id, int64_t tag_id);
int de_tag_delete_all(csilk_db_pool_t* pool, int64_t expense_id);
