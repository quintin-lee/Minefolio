#pragma once

#include <stdint.h>

/**
 * @brief 投资组合用例执行状态 DTO
 */
typedef struct portfolio_usecase_result {
    int  code;
    char message[256];
} portfolio_usecase_result_t;
