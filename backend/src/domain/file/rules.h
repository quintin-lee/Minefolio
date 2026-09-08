/**
 * @file rules.h
 * @brief 文件上传与导入业务规则声明 (Domain File Rules)
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief 校验文件是否支持解析
 */
bool mf_file_rule_validate_supported(const char* filename);

/**
 * @brief 校验文件大小是否在限制内
 */
bool mf_file_rule_validate_size(size_t len);

/**
 * @brief 校验 CSV 导入必填字段
 */
bool mf_file_rule_validate_csv_required(const char* date,
                                        const char* asset_name,
                                        const char* type,
                                        const char* amount);
