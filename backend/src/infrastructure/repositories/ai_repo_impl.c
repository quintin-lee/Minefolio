/**
 * @file ai_repo_impl.c
 * @brief AI 域仓储实现 (Infrastructure Layer)
 *
 * 包装 legacy repository 函数，为 AI tools 和 workflows 提供统一接口。
 */

#include "infrastructure/repositories/ai_repo_impl.h"
#include "infrastructure/repositories/market_repo_impl.h"
#include "repositories/asset_repo.h"
#include "repositories/daily_expense_repo.h"
#include "repositories/category_repo.h"
#include "repositories/transaction_repo.h"
#include "repositories/transfer_repo.h"

csilk_json_t*
mf_ai_repo_asset_list(
    void* pool, int64_t user_id, int64_t page, int64_t page_size, const char* type, int64_t* total)
{
    return asset_list(pool, user_id, page, page_size, type, total);
}

csilk_json_t*
mf_ai_repo_asset_get(void* pool, int64_t user_id, int64_t id)
{
    return asset_get(pool, user_id, id);
}

csilk_json_t*
mf_ai_repo_price_history_list(void* pool, int64_t user_id, int64_t asset_id, int64_t limit)
{
    return mf_market_repo_price_history_list(pool, user_id, asset_id, limit);
}
csilk_json_t*
mf_ai_repo_daily_expense_list(void*       pool,
                              int64_t     user_id,
                              int64_t     page,
                              int64_t     page_size,
                              const char* date_from,
                              const char* date_to,
                              int64_t     category_id,
                              int64_t*    total)
{
    char cat_id_str[32] = {0};
    if (category_id > 0) {
        snprintf(cat_id_str, sizeof(cat_id_str), "%lld", (long long)category_id);
    }
    return de_list(pool,
                   user_id,
                   page,
                   page_size,
                   NULL,
                   cat_id_str[0] ? cat_id_str : NULL,
                   NULL,
                   date_from,
                   date_to,
                   total);
}
int64_t
mf_ai_repo_daily_expense_insert(void*       pool,
                                int64_t     user_id,
                                const char* expense_date,
                                const char* expense_type,
                                double      amount,
                                const char* currency,
                                int64_t     category_id,
                                int64_t     asset_id,
                                const char* note)
{
    return de_insert(
        pool, user_id, category_id, asset_id, expense_type, amount, currency, expense_date, note);
}

csilk_json_t*
mf_ai_repo_category_list(void* pool, int64_t user_id, const char* type)
{
    return category_list(pool, user_id, type);
}
csilk_json_t*
mf_ai_repo_transaction_list(
    void* pool, int64_t user_id, int64_t page, int64_t page_size, const char* type, int64_t* total)
{
    return tx_list(pool, user_id, page, page_size, NULL, NULL, type, NULL, NULL, NULL, total);
}
int64_t
mf_ai_repo_transfer_insert(void*       pool,
                           int64_t     user_id,
                           int64_t     from_asset_id,
                           int64_t     to_asset_id,
                           double      amount,
                           const char* currency,
                           const char* note)
{
    return transfer_insert(pool, user_id, from_asset_id, to_asset_id, amount, currency, NULL, note);
}

csilk_json_t*
mf_ai_repo_daily_expense_monthly_by_category(void* pool, int64_t user_id, const char* pattern)
{
    return de_monthly_by_category(pool, user_id, pattern);
}

csilk_json_t*
mf_ai_repo_daily_expense_monthly_totals(void* pool, int64_t user_id, const char* pattern)
{
    return de_monthly_totals(pool, user_id, pattern);
}

csilk_json_t*
mf_ai_repo_transaction_monthly(void* pool, int64_t user_id, const char* pattern)
{
    return tx_monthly(pool, user_id, pattern);
}
