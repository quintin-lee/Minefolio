#pragma once

/**
 * @file dtos.h
 * @brief 标签用例结果 DTO (Tag Application DTOs)
 */

#include <stdint.h>

/** 标签用例通用结果 */
typedef struct {
    int  code;         /**< 错误码：0=成功, 1002=校验失败 */
    char message[256]; /**< 错误描述 */
} tag_usecase_result_t;
