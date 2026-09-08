/**
 * @file usecases.h
 * @brief 文件上传与导入用例接口声明 (Application File Use Cases)
 */

#pragma once

#include "domain/file/entity.h"
#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @brief 解析上传的文件
 */
int file_usecase_parse(void*                   pool,
                       const char*             data,
                       size_t                  data_len,
                       const char*             filename,
                       mf_file_parse_result_t* out);

/**
 * @brief 导入交易 CSV
 */
int file_usecase_import_transactions(
    void* pool, int64_t user_id, const char* csv_data, size_t csv_len, mf_import_result_t* out);

/**
 * @brief 导入日常收支 CSV
 */
int file_usecase_import_daily_expenses(
    void* pool, int64_t user_id, const char* csv_data, size_t csv_len, mf_import_result_t* out);
