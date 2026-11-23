/**
 * @file file_repo_impl.c
 * @brief 文件上传与导入仓储实现 (Infrastructure File Repository)
 *
 * 包装 services/file_parser.c 和 repositories/import_rule_repo.c 中的现有函数。
 */

#include "infrastructure/repositories/file_repo_impl.h"
#include "infrastructure/repositories/import_rule_repo_impl.h"
#include "common/db.h"
#include <string.h>

csilk_json_t*
mf_file_repo_get_import_rules(void* db_pool, int64_t user_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return mf_import_rule_list(pool, user_id);
}

int64_t
mf_file_repo_find_asset_by_name(void* db_pool, int64_t user_id, const char* name)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char*   params[] = {uid_str, name, NULL};
    csilk_json_t* res =
        csilk_db_query_param_json(pool, "SELECT id FROM assets WHERE user_id=? AND name=?", params);
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}

int64_t
mf_file_repo_find_category_by_name(void* db_pool, int64_t user_id, const char* name)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char             uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    const char*   params[] = {uid_str, name, NULL};
    csilk_json_t* res = csilk_db_query_param_json(
        pool, "SELECT id FROM categories WHERE user_id=? AND name=?", params);
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}
