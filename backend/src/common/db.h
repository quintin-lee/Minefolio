#pragma once
#include "csilk/drivers/db.h"

/** @brief Initialize database pool from environment/config. Returns 0 on success. */
int db_init(csilk_db_pool_t** out_pool);

/** @brief Run the migration SQL file against the pool. Returns 0 on success. */
int db_run_migrations(csilk_db_pool_t* pool);

/** @brief Get the global database pool singleton. */
csilk_db_pool_t* db_get_pool(void);
