#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>

csilk_json_t* cashflow_schedule_list(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t* cashflow_schedule_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int64_t       cashflow_schedule_create(csilk_db_pool_t* pool,
                                       int64_t          user_id,
                                       int64_t          source_asset_id,
                                       int64_t          target_asset_id,
                                       const char*      name,
                                       const char*      flow_type,
                                       const char*      frequency,
                                       const char*      start_date,
                                       const char*      end_date,
                                       double           expected_amount,
                                       const char*      note);
int           cashflow_schedule_update(csilk_db_pool_t* pool,
                                       int64_t          user_id,
                                       int64_t          id,
                                       int64_t          source_asset_id,
                                       int64_t          target_asset_id,
                                       const char*      name,
                                       const char*      flow_type,
                                       const char*      frequency,
                                       const char*      start_date,
                                       const char*      end_date,
                                       double           expected_amount,
                                       const char*      note);
int           cashflow_schedule_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
csilk_json_t* cashflow_list_active_schedules(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t*
cashflow_query_actual_transactions(csilk_db_pool_t* pool, int64_t user_id, const char* year_month);
