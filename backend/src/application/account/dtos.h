#pragma once

#include <stdint.h>

/**
 * @brief 账户用例执行结果 DTO (Account Use Case Result)
 */
typedef struct account_usecase_result {
    int     code; /**< 业务状态码: 0: 成功, 1002: 参数错误, 1003: 资源不存在, 1004: 冲突/禁止 */
    char    message[256];
    int64_t id;   /**< 创建或操作的账户 ID */
} account_usecase_result_t;
