/**
 * @file usecases.h
 * @brief 转账用例接口声明 (Application Transfer Use Cases)
 */

#pragma once

#include "application/transfer/commands.h"
#include "application/transfer/dtos.h"
#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @brief 创建转账（含余额调整）
 */
int transfer_usecase_create(void*                        pool,
                            const create_transfer_cmd_t* cmd,
                            transfer_usecase_result_t*   out_res);
