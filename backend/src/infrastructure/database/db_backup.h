#pragma once

/**
 * @file db_backup.h
 * @brief SQLite 官方在线热备份引擎、原子写与历史轮转接口
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "csilk/csilk.h"
#include "csilk/drivers/db.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 校验备份文件名是否合法且杜绝路径穿越
 * 仅允许包含字母、数字、下划线、减号及句点，且必须以 .db 结尾，长度在 5 到 128 字符之间。
 *
 * @param filename 待校验文件名
 * @return true 合法, false 包含非法字符或可能路径穿越
 */
bool db_backup_is_valid_filename(const char* filename);

/**
 * @brief 执行 SQLite 官方在线热备份（分片拷贝与微睡眠出让锁）
 * 先将数据写入 .db.tmp 临时文件，完成后原子 rename 成为标准 .db 文件。
 * 成功后自动执行轮转清理保留最近 7 份历史备份。
 *
 * @param pool 数据库连接池指针
 * @param backup_dir 备份文件存放目录（默认 "data/backups"）
 * @param out_filename 接收生成的备份文件名缓冲区
 * @param out_filename_size 缓冲区大小
 * @return 0 成功, -1 失败
 */
int db_backup_create(csilk_db_pool_t* pool, const char* backup_dir, char* out_filename, size_t out_filename_size);

/**
 * @brief 获取当前数据库存储指标、WAL 体积、磁盘剩余空间及备份历史列表
 *
 * @param pool 数据库连接池指针
 * @param backup_dir 备份文件存放目录
 * @param out_status 输出 JSON 包含 db_driver, db_size, wal_size, disk_free_bytes, backup_count, backups
 * @return 0 成功, -1 失败
 */
int db_backup_get_status(csilk_db_pool_t* pool, const char* backup_dir, csilk_json_t** out_status);

/**
 * @brief 按照 mtime 升序自动清理超过 max_keep 个数的历史旧备份
 *
 * @param backup_dir 备份文件存放目录
 * @param max_keep 最多保留的备份份数（默认 7）
 * @return 0 成功, -1 失败
 */
int db_backup_rotate(const char* backup_dir, int max_keep);

/**
 * @brief 删除指定的备份文件（受严格防路径穿越校验保护）
 *
 * @param backup_dir 备份文件存放目录
 * @param filename 文件名
 * @return 0 成功, -1 失败
 */
int db_backup_delete(const char* backup_dir, const char* filename);

#ifdef __cplusplus
}
#endif
