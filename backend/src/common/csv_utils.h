#pragma once

/**
 * @file csv_utils.h
 * @brief RFC 4180 标准 CSV 文本转义与行/字段流式解析工具接口
 *
 * 提供用于账单与交易数据导出时的 CSV 特殊字符转义（csv_escape）、
 * 以及导入解析时的双引号包裹/转义双引号字段提取（parse_csv_field）与整行多列解析（parse_csv_row）。
 */

#include <stddef.h>

/**
 * @brief 对单个字符串字段值执行 CSV 转义与包裹
 *
 * 遵循 RFC 4180 规范：
 * - 若字符串包含逗号 (,)、双引号 (")、换行符 (\n) 或回车符 (\r)，则将其整体用双引号包裹，并将内部所有双引号转义为双重双引号 ("")。
 * - 若不包含特殊字符，则直接复制原字符串。
 *
 * @param[out] out 接收转义后文本的输出缓冲区，不可为 NULL
 * @param[in] out_size 输出缓冲区容量大小
 * @param[in] val 待转义的原始字符串指针，若为 NULL 则输出空字符串 ""
 *
 * @note 内存所有权：由调用方分配和管理 out 缓冲区。
 * @note 线程安全性：纯字符串操作，线程安全。
 */
void csv_escape(char* out, size_t out_size, const char* val);

/**
 * @brief 从一行 CSV 文本的起始位置解析提取单个字段
 *
 * 支持标准 CSV 字段：
 * 1. 非引号字段：读取至遇到逗号 (,)、换行符 (\n) 或回车符 (\r) 结束。
 * 2. 引号字段：以双引号开头，支持内部连续双引号 ("") 转义解析为单个引号，直至遇到闭合双引号。
 *
 * @param[in] line 指向待解析行的起始字符指针
 * @param[in] len 待解析文本的最大有效长度
 * @param[out] out 接收解码后字段文本的输出缓冲区
 * @param[in] out_size 输出缓冲区容量大小
 * @param[out] chars_consumed 输出参数：从 line 中实际消耗扫描的字符总数（包括定界逗号/引号）
 *
 * @return int 状态码
 * @retval 0 解析成功
 *
 * @note 线程安全性：纯文本解析，线程安全。
 */
int
parse_csv_field(const char* line, size_t len, char* out, size_t out_size, size_t* chars_consumed);

/**
 * @brief 解析一行完整的 CSV 文本，提取最多 12 个字段
 *
 * 顺序调用 parse_csv_field 遍历一整行文本中的所有列，自动处理字段间分隔逗号以及行末的 CRLF / LF 换行符。
 *
 * @param[in] line 指向 CSV 行起始位置的字符指针
 * @param[in] len 该行文本的长度（包含行末换行符）
 * @param[out] out 固定大小的二维数组 (12 x 512)，用于存放解析出的各列字符串
 * @param[out] count 输出参数：实际成功解析的列数
 *
 * @return int 实际解析出的列数（即 *count）
 *
 * @note 内存所有权：out 数组由调用方提供。
 * @note 线程安全性：线程安全。
 */
int parse_csv_row(const char* line, size_t len, char out[12][512], int* count);
