#pragma once
#include "csilk/csilk.h"

/**
 * @file import_service.h
 * @brief 微信/支付宝/银行账单与通用 CSV 格式数据智能解析与批量导入服务
 */

/**
 * @brief 导入投资交易流水 CSV (POST /api/transactions/import)
 * @param c HTTP 上下文（支持 CSV 文本或文件上传）
 */
void transactions_import_csv(csilk_ctx_t* c);

/**
 * @brief 导入日常收支流水 CSV (POST /api/daily-expenses/import)
 * 自动匹配智能分类规则与对应资产账户
 * @param c HTTP 上下文
 */
void daily_expenses_import_csv(csilk_ctx_t* c);
