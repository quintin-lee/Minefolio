/**
 * @file import_export_controller.h
 * @brief 财务数据导入与导出控制器头文件 (DDD 接口层)
 */

#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @brief 导出交易记录为 CSV 格式文件 (GET /api/export/transactions)
 */
void transactions_export_csv(csilk_ctx_t* c);

/**
 * @brief 从 CSV 文件批量导入交易记录 (POST /api/import/transactions)
 */
void transactions_import_csv(csilk_ctx_t* c);

/**
 * @brief 导出日常收支记账明细为 CSV 文件 (GET /api/export/daily-expenses)
 */
void daily_expenses_export_csv(csilk_ctx_t* c);

/**
 * @brief 从 CSV 文件批量导入日常收支记账 (POST /api/import/daily-expenses)
 */
void daily_expenses_import_csv(csilk_ctx_t* c);

/**
 * @brief 注册数据导入与导出相关的所有 HTTP 路由
 */
void register_import_export_routes(csilk_app_t* app);
