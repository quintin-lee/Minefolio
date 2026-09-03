#pragma once

#include <stdint.h>

/**
 * @brief 资产用例执行结果 DTO (Asset Use Case Result)
 */
typedef struct asset_usecase_result {
    int     code;          /**< 业务状态码: 0: 成功, 1002: 参数错误, 1003: 资源不存在, 500: 内部错误 */
    char    message[256];  /**< 错误描述或响应提示 */
    int64_t id;            /**< 创建或操作的资产 ID */
} asset_usecase_result_t;
