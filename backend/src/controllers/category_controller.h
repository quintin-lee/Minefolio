#pragma once

/**
 * @brief Seed default category templates (asset/expense/income/transaction)
 *        for a user. No-op for types that already have categories.
 *        Called once at user creation (setup/register).
 */
void categories_seed_defaults(csilk_db_pool_t* pool, int64_t user_id);
