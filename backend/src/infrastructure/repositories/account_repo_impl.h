#pragma once

#include "csilk/csilk.h"
#include "domain/account/repository.h"
#include "domain/account/rules.h"
#include "domain/account/entity.h"
#include <stdint.h>

/* 基础设施层：基于 SQL 数据库实现账户仓储契约 */

csilk_json_t* account_list(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           int64_t          ledger_id,
                           bool             include_archived,
                           int64_t*         total);
csilk_json_t* account_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int64_t       account_insert(csilk_db_pool_t* pool,
                             int64_t          user_id,
                             int64_t          ledger_id,
                             const char*      name,
                             const char*      institution,
                             const char*      account_no,
                             const char*      currency,
                             const char*      icon,
                             const char*      note,
                             int64_t          sort_order);
int           account_update(csilk_db_pool_t* pool,
                             int64_t          user_id,
                             int64_t          id,
                             const char*      name,
                             const char*      institution,
                             const char*      account_no,
                             const char*      currency,
                             const char*      icon,
                             const char*      note,
                             int64_t          sort_order,
                             int              has_sort_order,
                             int              has_archived,
                             int              archived);
int           account_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int           account_count_assets(csilk_db_pool_t* pool,
                                   int64_t          user_id,
                                   int64_t          account_id,
                                   int64_t*         out_count);
int
account_has_transactions(csilk_db_pool_t* pool, int64_t user_id, int64_t account_id, int* out_flag);
int64_t account_find_default(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id);
int64_t account_create_default(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id);
