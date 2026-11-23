#pragma once

#include "core/ledger/ledger_engine.h"
#include "infrastructure/repositories/transaction_lot_repo_impl.h"
#include "common/db.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Check whether an asset uses FIFO cost basis tracking (scoped by user_id).
 */
bool ledger_internal_asset_is_fifo(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);

/**
 * @brief Compute per-unit cost basis (including fee) and insert an open FIFO buy lot.
 */
int64_t ledger_internal_create_buy_lot(csilk_db_pool_t* pool,
                                       int64_t          user_id,
                                       int64_t          asset_id,
                                       int64_t          tx_id,
                                       const char*      tx_date,
                                       money_t          amount,
                                       price_t          price,
                                       quantity_t       qty,
                                       money_t          fee);
