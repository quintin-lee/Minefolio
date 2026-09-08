#pragma once

/**
 * @file dtos.h
 * @brief 分类用例结果 DTO (Category Application DTOs)
 */

#include <stdint.h>

/** 分类用例通用结果 */
typedef struct {
    int  code;
    char message[256];
} category_usecase_result_t;
