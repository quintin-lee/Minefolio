#pragma once

/**
 * @file lock.h
 * @brief 数据库迁移并发互斥锁（防止多实例同时启动执行 migration）
 */

#include "infrastructure/database/database.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化锁表结构与基础行 (id=1)
 * @param db 数据库句柄
 * @return 0 成功，-1 失败
 */
int mf_migration_lock_init(mf_db_t* db);

/**
 * @brief 获取迁移互斥锁（支持超时与忙时等待）
 * @param db 数据库句柄
 * @param locked_by 锁定者标识（如主机名/进程号）
 * @param timeout_seconds 超时秒数（<=0 表示尝试一次）
 * @return 0 成功获取锁，-1 获取失败或超时
 */
int mf_migration_lock_acquire(mf_db_t* db, const char* locked_by, int timeout_seconds);

/**
 * @brief 释放迁移互斥锁
 * @param db 数据库句柄
 * @param locked_by 锁定者标识
 * @return 0 成功释放，-1 失败
 */
int mf_migration_lock_release(mf_db_t* db, const char* locked_by);

#ifdef __cplusplus
}
#endif
