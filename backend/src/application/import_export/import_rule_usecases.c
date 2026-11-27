#include "application/import_export/import_rule_usecases.h"
#include "infrastructure/repositories/import_rule_repo_impl.h"

csilk_json_t*
import_rule_usecase_list_paged(
    void* pool, int64_t user_id, int64_t page, int64_t page_size, int64_t* out_total)
{
    if (!pool || user_id <= 0) {
        if (out_total) {
            *out_total = 0;
        }
        return NULL;
    }
    return mf_import_rule_list_paged((csilk_db_pool_t*)pool, user_id, page, page_size, out_total);
}

csilk_json_t*
import_rule_usecase_list(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return NULL;
    }
    return mf_import_rule_list((csilk_db_pool_t*)pool, user_id);
}

csilk_json_t*
import_rule_usecase_get(void* pool, int64_t user_id, int64_t id)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return NULL;
    }
    return mf_import_rule_get((csilk_db_pool_t*)pool, user_id, id);
}

int64_t
import_rule_usecase_create(void*       pool,
                           int64_t     user_id,
                           const char* keyword,
                           const char* match_field,
                           const char* match_type,
                           int64_t     category_id,
                           const char* target_type,
                           int         priority,
                           int         is_active)
{
    if (!pool || user_id <= 0 || !keyword || !keyword[0]) {
        return -1;
    }
    return mf_import_rule_insert((csilk_db_pool_t*)pool,
                                 user_id,
                                 keyword,
                                 match_field ? match_field : "all",
                                 match_type ? match_type : "contains",
                                 category_id,
                                 target_type ? target_type : "expense",
                                 priority > 0 ? priority : 100,
                                 is_active);
}

int
import_rule_usecase_update(void*       pool,
                           int64_t     user_id,
                           int64_t     id,
                           const char* keyword,
                           const char* match_field,
                           const char* match_type,
                           int64_t     category_id,
                           const char* target_type,
                           int         priority,
                           int         is_active)
{
    if (!pool || user_id <= 0 || id <= 0 || !keyword || !keyword[0]) {
        return 0;
    }
    return mf_import_rule_update((csilk_db_pool_t*)pool,
                                 user_id,
                                 id,
                                 keyword,
                                 match_field ? match_field : "all",
                                 match_type ? match_type : "contains",
                                 category_id,
                                 target_type ? target_type : "expense",
                                 priority > 0 ? priority : 100,
                                 is_active);
}

int
import_rule_usecase_delete(void* pool, int64_t user_id, int64_t id)
{
    if (!pool || user_id <= 0 || id <= 0) {
        return -1;
    }
    return mf_import_rule_delete((csilk_db_pool_t*)pool, user_id, id);
}

csilk_json_t*
import_rule_usecase_reset_defaults(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return NULL;
    }
    mf_import_rule_reset_defaults((csilk_db_pool_t*)pool, user_id);
    return mf_import_rule_list((csilk_db_pool_t*)pool, user_id);
}
