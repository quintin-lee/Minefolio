#pragma once
#include "csilk/csilk.h"

/**
 * @file export_service.h
 * @brief 数据导出服务（投资交易流水与日常收支 CSV 文件流导出）
 */

/**
 * @brief 导出投资交易流水为标准 CSV 文件 (GET /api/transactions/export)
 * @param c HTTP 上下文（支持筛选条件过滤导出范围）
 */
void transactions_export_csv(csilk_ctx_t* c);

/**
 * @brief 导出日常收支明细为标准 CSV 文件 (GET /api/daily-expenses/export)
 * @param c HTTP 上下文（支持时间范围与分类过滤）
 */
void daily_expenses_export_csv(csilk_ctx_t* c);
