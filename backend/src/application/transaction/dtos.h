#pragma once

#include <stdint.h>
#include <stddef.h>
#include "domain/transaction/entity.h"

/**
 * @brief 用例统一执行结果对象
 */
typedef struct tx_usecase_result {
    int         code;             /* 0: 成功; 1002: 参数错误; 1003: 未找到; 1004: 冲突/系统错误 */
    char        message[256];     /* 错误描述或提示信息 */
    int64_t     created_id;       /* 创建成功的实体 ID */
    void*       data_payload;     /* 结果载荷数据 (如实体或列表) */
    int64_t     total;            /* 分页查询总数 */
} tx_usecase_result_t;
