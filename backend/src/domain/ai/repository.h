/**
 * @file repository.h
 * @brief AI 域仓储契约接口 (Domain AI Repository Interface)
 *
 * 为 AI tools 和 workflows 提供统一的数据访问接口，
 * 隔离对 legacy repository 的直接依赖。
 */

#pragma once

#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @brief 获取用户资产列表
 */
csilk_json_t* mf_ai_repo_asset_list(
    void* pool, int64_t user_id, int64_t page, int64_t page_size, const char* type, int64_t* total);

/**
 * @brief 获取单个资产详情
 */
csilk_json_t* mf_ai_repo_asset_get(void* pool, int64_t user_id, int64_t id);

/**
 * @brief 获取资产价格历史
 */
csilk_json_t*
mf_ai_repo_price_history_list(void* pool, int64_t user_id, int64_t asset_id, int64_t limit);

/**
 * @brief 获取用户日常收支列表
 */
csilk_json_t* mf_ai_repo_daily_expense_list(void*       pool,
                                            int64_t     user_id,
                                            int64_t     page,
                                            int64_t     page_size,
                                            const char* date_from,
                                            const char* date_to,
                                            int64_t     category_id,
                                            int64_t*    total);

/**
 * @brief 插入日常收支记录
 */
int64_t mf_ai_repo_daily_expense_insert(void*       pool,
                                        int64_t     user_id,
                                        const char* expense_date,
                                        const char* expense_type,
                                        double      amount,
                                        const char* currency,
                                        int64_t     category_id,
                                        int64_t     asset_id,
                                        const char* note);

/**
 * @brief 获取用户分类列表
 */
csilk_json_t* mf_ai_repo_category_list(void* pool, int64_t user_id, const char* type);

/**
 * @brief 获取用户交易列表
 */
csilk_json_t* mf_ai_repo_transaction_list(
    void* pool, int64_t user_id, int64_t page, int64_t page_size, const char* type, int64_t* total);

/**
 * @brief 插入转账记录
 */
int64_t mf_ai_repo_transfer_insert(void*       pool,
                                   int64_t     user_id,
                                   int64_t     from_asset_id,
                                   int64_t     to_asset_id,
                                   double      amount,
                                   const char* currency,
                                   const char* note);

/**
 * @brief 获取日常收支按分类月度汇总
 */
csilk_json_t*
mf_ai_repo_daily_expense_monthly_by_category(void* pool, int64_t user_id, const char* pattern);

/**
 * @brief 获取日常收支月度总额
 */
csilk_json_t*
mf_ai_repo_daily_expense_monthly_totals(void* pool, int64_t user_id, const char* pattern);

/**
 * @brief 获取交易月度汇总
 */
csilk_json_t* mf_ai_repo_transaction_monthly(void* pool, int64_t user_id, const char* pattern);
