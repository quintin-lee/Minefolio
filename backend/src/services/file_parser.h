#pragma once
#include "csilk/csilk.h"

int file_parse(csilk_db_pool_t* pool,
               const char*      data,
               size_t           data_len,
               const char*      filename,
               char*            out,
               size_t           out_len);

char* file_parse_to_string(
    csilk_db_pool_t* pool, const char* data, size_t data_len, const char* filename, size_t max_len);
