#pragma once

/**
 * @file checksum.h
 * @brief 迁移脚本 SHA-256 跨平台规范化校验和计算
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 计算 SQL 迁移文件的规范化 SHA-256 校验和 (64 位小写十六进制)
 *
 * 在计算前自动抹除 CRLF 差异（将 \r\n 规整为 \n）并修剪末尾空白，
 * 防止跨平台 Git 检出导致校验和不匹配。
 *
 * @param filepath 文件路径
 * @param[out] out_checksum 缓冲区（至少 65 字节）
 * @return 0 成功，-1 失败
 */
int mf_migration_checksum_file(const char* filepath, char out_checksum[65]);

/**
 * @brief 计算文本内容的规范化 SHA-256 校验和
 *
 * @param content 输入文本
 * @param len 文本长度
 * @param[out] out_checksum 缓冲区（至少 65 字节）
 * @return 0 成功，-1 失败
 */
int mf_migration_checksum_content(const char* content, size_t len, char out_checksum[65]);

#ifdef __cplusplus
}
#endif
