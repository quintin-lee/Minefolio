#pragma once

#include "csilk/csilk.h"

/**
 * @file transaction_controller.h
 * @brief Interfaces 表现层交易 HTTP 控制器头文件
 * @note 极薄控制器：仅负责 HTTP 报文解析与 DTO/Command 转换，严禁包含任何业务逻辑
 */

void api_transactions_list(csilk_ctx_t* c);
void api_transactions_monthly(csilk_ctx_t* c);
void api_transactions_create(csilk_ctx_t* c);
void api_transactions_update(csilk_ctx_t* c);
void api_transactions_delete(csilk_ctx_t* c);

void register_transaction_routes(csilk_app_t* app);
void register_interfaces_transaction_routes(csilk_app_t* app);
