/**
 * @file file_repo_impl.c
 * @brief 文件上传与导入仓储实现 (Infrastructure File Repository)
 *
 * 包装 services/file_parser.c 和 repositories/import_rule_repo.c 中的现有函数。
 */

#include "infrastructure/repositories/file_repo_impl.h"
#include "services/file_parser.h"
#include "repositories/import_rule_repo.h"
#include "common/db.h"
#include <string.h>

int
mf_file_repo_parse(void*                   db_pool,
                   const char*             data,
                   size_t                  data_len,
                   const char*             filename,
                   mf_file_parse_result_t* out)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;

    strncpy(out->filename, filename, sizeof(out->filename) - 1);
    out->size = data_len;

    int rc = file_parse(pool, data, data_len, filename, out->content, sizeof(out->content));
    if (rc == 0) {
        strncpy(out->status, "parsed", sizeof(out->status) - 1);
    } else {
        strncpy(out->status, "error", sizeof(out->status) - 1);
        strncpy(out->error, "failed to parse file", sizeof(out->error) - 1);
    }
    return rc;
}

csilk_json_t*
mf_file_repo_get_import_rules(void* db_pool, int64_t user_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    return import_rule_list(pool, user_id);
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
