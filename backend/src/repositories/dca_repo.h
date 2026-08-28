#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>

csilk_json_t* dca_plan_list(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t* dca_plan_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int64_t       dca_plan_create(csilk_db_pool_t* pool,
                              int64_t          user_id,
                              int64_t          target_asset_id,
                              int64_t          funding_asset_id,
                              const char*      name,
                              const char*      frequency,
                              int              day_of_period,
                              double           amount,
                              double           target_profit_rate,
                              double           target_total_amount,
                              int              target_total_periods,
                              const char*      note);
int           dca_plan_update(csilk_db_pool_t* pool,
                              int64_t          user_id,
                              int64_t          id,
                              int64_t          target_asset_id,
                              int64_t          funding_asset_id,
                              const char*      name,
                              const char*      frequency,
                              int              day_of_period,
                              double           amount,
                              double           target_profit_rate,
                              double           target_total_amount,
                              int              target_total_periods,
                              const char*      note);
int dca_plan_set_status(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* status);
int dca_plan_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
csilk_json_t* dca_plan_list_all_active(csilk_db_pool_t* pool);

int64_t       dca_execution_create(csilk_db_pool_t* pool,
                                   int64_t          plan_id,
                                   int64_t          user_id,
                                   const char*      period_date,
                                   double           planned_amount);
csilk_json_t* dca_execution_list_by_plan(csilk_db_pool_t* pool, int64_t user_id, int64_t plan_id);
csilk_json_t* dca_execution_list_pending(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t* dca_execution_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int           dca_execution_update_confirmed(csilk_db_pool_t* pool,
                                             int64_t          id,
                                             double           actual_amount,
                                             double           executed_price,
                                             double           executed_quantity,
                                             int64_t          transaction_id);
int
dca_execution_update_status(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* status);
