#pragma once
#include "csilk/csilk.h"

/**
 * @brief 创建带指定后缀的安全临时文件 (POSIX mkstemps 包装)
 */
int file_parser_write_temp(
    const char* data, size_t len, const char* suffix, char* out_path, size_t out_path_len);

int file_parse(csilk_db_pool_t* pool,
               const char*      data,
               size_t           data_len,
               const char*      filename,
               char*            out,
               size_t           out_len);

char* file_parse_to_string(
    csilk_db_pool_t* pool, const char* data, size_t data_len, const char* filename, size_t max_len);
