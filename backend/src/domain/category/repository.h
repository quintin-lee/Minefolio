#pragma once

#include <stdint.h>
#include <stddef.h>
#include "domain/category/entity.h"

int mf_category_repo_list(void*           db_pool,
                          int64_t         user_id,
                          int64_t         ledger_id,
                          const char*     type,
                          mf_category_t** out_list,
                          size_t*         out_count);
int mf_category_repo_children(void*           db_pool,
                              int64_t         user_id,
                              int64_t         ledger_id,
                              int64_t         parent_id,
                              mf_category_t** out_list,
                              size_t*         out_count);
int mf_category_repo_count_children(
    void* db_pool, int64_t user_id, int64_t ledger_id, int64_t parent_id, int64_t* out_count);
int mf_category_repo_create(
    void* db_pool, int64_t user_id, int64_t ledger_id, const mf_category_t* cat, int64_t* out_id);
int mf_category_repo_update(
    void* db_pool, int64_t user_id, int64_t ledger_id, int64_t id, const mf_category_t* cat);
int     mf_category_repo_delete(void* db_pool, int64_t user_id, int64_t ledger_id, int64_t id);
int64_t mf_category_repo_find_or_create(void*       db_pool,
                                        int64_t     user_id,
                                        int64_t     ledger_id,
                                        const char* name,
                                        int64_t     parent_id,
                                        const char* type,
                                        const char* asset_type,
                                        const char* icon,
                                        int         sort_order);
int     mf_category_repo_exists(void* db_pool, int64_t user_id, int64_t ledger_id, int64_t id);
int     mf_category_repo_is_seeded(void* db_pool, int64_t user_id, int64_t ledger_id);
void    mf_category_repo_mark_seeded(void* db_pool, int64_t user_id, int64_t ledger_id);
void    mf_category_repo_free_list(mf_category_t* list, size_t count);
