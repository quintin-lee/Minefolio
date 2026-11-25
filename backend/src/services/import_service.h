#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @file import_service.h
 * @brief 微信/支付宝/银行账单与通用 CSV 格式数据智能解析与批量导入服务
 */

#define MF_MAX_CSV_IMPORT_BYTES ((size_t)(10 * 1024 * 1024))

int import_transactions_csv_execute(csilk_db_pool_t* pool,
                                    int64_t          task_id,
                                    int64_t          user_id,
                                    int64_t          ledger_id,
                                    const char*      csv_data,
                                    size_t           csv_len,
                                    char**           out_result_json,
                                    char*            err_buf,
                                    size_t           err_cap);

int import_daily_expenses_csv_execute(csilk_db_pool_t* pool,
                                      int64_t          task_id,
                                      int64_t          user_id,
                                      int64_t          ledger_id,
                                      const char*      csv_data,
                                      size_t           csv_len,
                                      char**           out_result_json,
                                      char*            err_buf,
                                      size_t           err_cap);

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
