#pragma once
#include "csilk/csilk.h"
#include "core/financial/quantity.h"
#include "core/financial/price.h"
#include "core/financial/money.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t tx_lot_create(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          asset_id,
                      int64_t          buy_tx_id,
                      const char*      buy_date,
                      quantity_t       qty,
                      price_t          cost_per_unit,
                      money_t          fee);

csilk_json_t* tx_lots_get_open(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);

int tx_lot_deduct(csilk_db_pool_t* pool,
                  int64_t          lot_id,
                  quantity_t       deduct_qty,
                  quantity_t       new_remaining_qty,
                  bool             is_closed);

int tx_lots_delete_by_asset(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);

int tx_lots_delete_by_buy_tx(csilk_db_pool_t* pool, int64_t user_id, int64_t buy_tx_id);

char* tx_lots_serialize_open_json(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);

#ifdef __cplusplus
}
#endif
