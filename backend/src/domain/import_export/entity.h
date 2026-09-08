/**
 * @file entity.h
 * @brief 导入导出领域实体定义 (Domain Import/Export Entity)
 *
 * 纯 C 结构体，零外部依赖。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 导入结果实体
 */
typedef struct {
    int64_t total_rows;    /**< 总行数 */
    int64_t success_count; /**< 成功导入行数 */
    int64_t error_count;   /**< 失败行数 */
    char    errors[2048];  /**< 错误信息汇总 */
} mf_import_result_t;

/**
 * @brief 导出格式类型
 */
typedef enum {
    MF_EXPORT_CSV, /**< CSV 格式 */
    MF_EXPORT_JSON /**< JSON 格式 */
} mf_export_format_t;
