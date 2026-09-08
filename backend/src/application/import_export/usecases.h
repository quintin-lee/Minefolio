/**
 * @file usecases.h
 * @brief 导入导出用例接口声明 (Application Layer)
 */

#pragma once

#include "csilk/csilk.h"

/**
 * @brief 导出交易记录为 CSV 用例
 */
void import_export_usecase_transactions_export(csilk_ctx_t* c);

/**
 * @brief 导入交易记录用例
 */
void import_export_usecase_transactions_import(csilk_ctx_t* c);

/**
 * @brief 导出日常收支为 CSV 用例
 */
void import_export_usecase_daily_expenses_export(csilk_ctx_t* c);

/**
 * @brief 导入日常收支用例
 */
void import_export_usecase_daily_expenses_import(csilk_ctx_t* c);
