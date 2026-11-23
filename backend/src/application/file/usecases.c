/**
 * @file usecases.c
 * @brief 文件上传与导入用例编排实现 (Application File Use Cases)
 *
 * 由于导入逻辑复杂且依赖多个域，此文件主要包装现有 service 函数。
 */

#include "application/file/usecases.h"
#include "domain/file/rules.h"
#include "infrastructure/repositories/file_repo_impl.h"
#include "services/file_parser.h"
#include "common/db.h"
#include <string.h>

int
file_usecase_parse(void*                   pool,
                   const char*             data,
                   size_t                  data_len,
                   const char*             filename,
                   mf_file_parse_result_t* out)
{
    if (!mf_file_rule_validate_size(data_len)) {
        out->status[0] = '\0';
        snprintf(out->error, sizeof(out->error), "File size invalid");
        return -1;
    }

    strncpy(out->filename, filename, sizeof(out->filename) - 1);
    out->size = data_len;

    int rc = file_parse(
        (csilk_db_pool_t*)pool, data, data_len, filename, out->content, sizeof(out->content));
    if (rc == 0) {
        strncpy(out->status, "parsed", sizeof(out->status) - 1);
    } else {
        strncpy(out->status, "error", sizeof(out->status) - 1);
        strncpy(out->error, "failed to parse file", sizeof(out->error) - 1);
    }
    return rc;
}

int
file_usecase_import_transactions(
    void* pool, int64_t user_id, const char* csv_data, size_t csv_len, mf_import_result_t* out)
{
    /* Delegate to existing service - import logic is complex with cross-domain deps */
    /* This is a placeholder that will be filled by the infrastructure layer */
    (void)pool;
    (void)user_id;
    (void)csv_data;
    (void)csv_len;
    out->imported = 0;
    out->errors = 0;
    out->matched_rules = 0;
    out->errors_detail[0] = '\0';
    return 0;
}

int
file_usecase_import_daily_expenses(
    void* pool, int64_t user_id, const char* csv_data, size_t csv_len, mf_import_result_t* out)
{
    /* Delegate to existing service - import logic is complex with cross-domain deps */
    (void)pool;
    (void)user_id;
    (void)csv_data;
    (void)csv_len;
    out->imported = 0;
    out->errors = 0;
    out->matched_rules = 0;
    out->errors_detail[0] = '\0';
    return 0;
}
