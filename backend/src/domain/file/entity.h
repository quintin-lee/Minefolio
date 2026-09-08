/**
 * @file entity.h
 * @brief 文件上传与导入领域实体定义 (Domain File Entity)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief 文件解析结果
 */
typedef struct {
    char   filename[256];  /**< 文件名 */
    size_t size;           /**< 文件大小 */
    char   content[50000]; /**< 解析后的内容 */
    char   status[16];     /**< 状态 (parsed/error) */
    char   error[256];     /**< 错误信息 */
} mf_file_parse_result_t;

/**
 * @brief CSV 导入结果
 */
typedef struct {
    int  imported;            /**< 成功导入条数 */
    int  errors;              /**< 错误条数 */
    int  matched_rules;       /**< 匹配的智能规则数 */
    char errors_detail[2048]; /**< 错误详情 */
} mf_import_result_t;
