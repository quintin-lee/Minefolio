#pragma once

#include "domain/asset/repository.h"
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>

/* 基础设施层：基于 SQL 数据库实现纯领域资产仓储契约 */

/* Legacy function signatures for backward compatibility.
 *
 * `caller_user_id` semantics:
 *   - 0        => "owner / self" view: behaves EXACTLY as before (owner, AI tools, scheduler
 *                  paths are unchanged). The caller's own `user_id` is the data owner.
 *   - == user_id (self) => owner view, same as 0.
 *   - > 0 && != user_id => "shared-ledger member" view: reads the ledger OWNER's assets
 *                  (user_id), but restricted to assets in a ledger the caller is a member of,
 *                  and the owner has not explicitly hidden the owning account (account_permissions
 *                  permission_level = 'none').
 *
 * This lets a ledger member (V004 ledger_members) see the owner's asset data in a shared ledger,
 * while the owner and all AI/workflow/scheduler read paths keep the legacy behaviour untouched.
 */
csilk_json_t* asset_list(csilk_db_pool_t* pool,
                         int64_t          user_id,
                         int64_t          caller_user_id,
                         int64_t          ledger_id,
                         int64_t          page,
                         int64_t          page_size,
                         const char*      category_id,
                         int64_t          account_id,
                         int64_t*         total);
csilk_json_t* asset_get(csilk_db_pool_t* pool, int64_t user_id, int64_t caller_user_id, int64_t id);
csilk_json_t* asset_get_raw(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int           asset_get_ledger_and_account(csilk_db_pool_t* pool,
                                           int64_t          asset_id,
                                           int64_t*         out_ledger_id,
                                           int64_t*         out_account_id);
int           asset_get_currency_for_user(csilk_db_pool_t* pool,
                                          int64_t          user_id,
                                          int64_t          asset_id,
                                          char*            out_currency,
                                          size_t           out_cap);
int64_t       asset_insert(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           int64_t          category_id,
                           int64_t          account_id,
                           const char*      name,
                           const char*      account_no,
                           double           current_value,
                           const char*      currency,
                           const char*      note,
                           double           quantity,
                           double           cost_basis,
                           double           net_value,
                           const char*      symbol,
                           const char*      quote_source,
                           const char*      cost_method);
int           asset_update_basic(csilk_db_pool_t* pool,
                                 int64_t          user_id,
                                 int64_t          id,
                                 const char*      name,
                                 const char*      account_no,
                                 double           current_value,
                                 const char*      currency,
                                 const char*      note,
                                 const char*      symbol,
                                 const char*      quote_source);
int asset_update_account(csilk_db_pool_t* pool, int64_t user_id, int64_t id, int64_t account_id);
int asset_update_cost_method(csilk_db_pool_t* pool,
                             int64_t          user_id,
                             int64_t          id,
                             const char*      cost_method);
int asset_update_position(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          int64_t          id,
                          double           net_value,
                          double           quantity,
                          double           cost_basis);
int asset_update_market_quote(csilk_db_pool_t* pool,
                              int64_t          user_id,
                              int64_t          asset_id,
                              double           new_net_value);
csilk_json_t* asset_list_for_sync(csilk_db_pool_t* pool, int64_t user_id);
int asset_has_transactions(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, int* out_flag);
int asset_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int64_t       asset_get_account_id(csilk_db_pool_t* pool, int64_t asset_id);
char*         asset_get_category_type(csilk_db_pool_t* pool, int64_t user_id, int64_t category_id);
csilk_json_t* asset_transactions(csilk_db_pool_t* pool,
                                 int64_t          user_id,
                                 int64_t          caller_user_id,
                                 int64_t          asset_id);
csilk_json_t* asset_balance_logs_list(csilk_db_pool_t* pool,
                                      int64_t          user_id,
                                      int64_t          caller_user_id,
                                      int64_t          page,
                                      int64_t          page_size,
                                      const char*      asset_id_str,
                                      int64_t*         total);

/**
 * @brief 汇总指定资产在 [start_date, end_date] 区间的余额变动 delta 总和
 * @return 0 成功 (out_sum 写入 double 合计，负债方向已翻转)；-1 失败
 */
int asset_daily_balance_log_sum(csilk_db_pool_t* pool,
                                int64_t          user_id,
                                int64_t          asset_id,
                                const char*      start_date,
                                const char*      end_date,
                                double*          out_sum);

int           asset_count_transactions(void* pool, int64_t user_id, int64_t asset_id);
int64_t       asset_get_ledger_id(void* pool, int64_t user_id, int64_t asset_id);
csilk_json_t* asset_list_values_with_type(void* pool, int64_t user_id, int64_t ledger_id);
int           asset_get_currency(void* pool, int64_t asset_id, char* out_currency, size_t max_len);
