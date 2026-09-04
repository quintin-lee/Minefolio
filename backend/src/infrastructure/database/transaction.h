#pragma once

/**
 * @file transaction.h
 * @brief 显式事务管理与保存点 (Savepoint) 接口
 */

#include "infrastructure/database/database.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 开启新事务并签出专用物理连接
 * @param db 数据库句柄
 * @param out_tx 输出事务句柄
 * @return 0 成功，-1 失败
 */
int mf_tx_begin(mf_db_t* db, mf_tx_t** out_tx);

/**
 * @brief 提交事务并释放连接
 * @param tx 事务句柄
 * @return 0 成功，-1 失败
 */
int mf_tx_commit(mf_tx_t* tx);

/**
 * @brief 回滚事务并释放连接
 * @param tx 事务句柄
 * @return 0 成功，-1 失败
 */
int mf_tx_rollback(mf_tx_t* tx);

/**
 * @brief 设立保存点
 * @param tx 事务句柄
 * @param name 保存点名称
 * @return 0 成功，-1 失败
 */
int mf_tx_savepoint(mf_tx_t* tx, const char* name);

/**
 * @brief 回滚至指定保存点
 * @param tx 事务句柄
 * @param name 保存点名称
 * @return 0 成功，-1 失败
 */
int mf_tx_rollback_to_savepoint(mf_tx_t* tx, const char* name);

/**
 * @brief 释放保存点
 * @param tx 事务句柄
 * @param name 保存点名称
 * @return 0 成功，-1 失败
 */
int mf_tx_release_savepoint(mf_tx_t* tx, const char* name);

/**
 * @brief 在当前事务连接中执行非查询 SQL
 * @param tx 事务句柄
 * @param sql 待执行的 SQL
 * @return 0 成功，-1 失败
 */
int mf_tx_execute(mf_tx_t* tx, const char* sql);

/**
 * @brief 在事务专用连接中创建预编译语句
 * @param tx 事务句柄
 * @param sql 参数化 SQL 语句
 * @param out_stmt 输出语句句柄
 * @return 0 成功，-1 失败
 */
int mf_tx_prepare(mf_tx_t* tx, const char* sql, mf_stmt_t** out_stmt);

#ifdef __cplusplus
}
#endif
