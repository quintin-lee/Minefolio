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
