#pragma once

/**
 * @file receipt_usecases.h
 * @brief 票据 AI OCR 识别应用层用例接口
 */

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 提交异步票据识别任务（若提交失败自动释放内部副本）
 * @return > 0 为任务 ID；<= 0 表示参数无效或提交失败
 */
int64_t receipt_usecase_scan_submit(void*       pool,
                                    int64_t     user_id,
                                    const char* image_url,
                                    const char* model_override,
                                    const char* provider_override,
                                    bool*       out_deduplicated);
