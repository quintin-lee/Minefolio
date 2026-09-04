#pragma once

/**
 * @file database.h
 * @brief 统一数据库基础设施接口与连接池/重试生命周期管理
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MF_DB_ENGINE_SQLITE = 0, MF_DB_ENGINE_POSTGRES = 1 } mf_db_engine_t;

typedef struct {
    mf_db_engine_t engine;
    const char*    dsn;
    int            min_connections;
    int            max_connections;
    int            idle_timeout_ms;
    int            busy_timeout_ms;
    int            max_retries;
    int            retry_interval_ms;
} mf_db_config_t;

typedef struct mf_db_s     mf_db_t;
typedef struct mf_tx_s     mf_tx_t;
typedef struct mf_stmt_s   mf_stmt_t;
typedef struct mf_result_s mf_result_t;

/**
 * @brief 打开并初始化数据库连接抽象
 * @param config 数据库配置
 * @param out_db 输出数据库句柄
 * @return 0 成功，非 0 失败
 */
int mf_db_open(const mf_db_config_t* config, mf_db_t** out_db);

/**
 * @brief 包装既有 csilk_db_pool_t 连接池为统一数据库抽象
 * @param csilk_pool 底层 csilk 连接池
 * @param engine 引擎类型
 * @param out_db 输出句柄
 * @return 0 成功，-1 失败
 */
int mf_db_wrap_csilk(void* csilk_pool, mf_db_engine_t engine, mf_db_t** out_db);

/**
 * @brief 关闭并释放数据库句柄
 * @param db 数据库句柄
 */
void mf_db_close(mf_db_t* db);

/**
 * @brief 获取当前数据库引擎类型
 * @param db 数据库句柄
 * @return 引擎枚举
 */
mf_db_engine_t mf_db_get_engine(const mf_db_t* db);

/**
 * @brief 执行无返回结果的非事务 SQL 语句
 * @param db 数据库句柄
 * @param sql 待执行的 SQL
 * @return 0 成功，-1 失败
 */
int mf_db_execute(mf_db_t* db, const char* sql);

/**
 * @brief 带冲突退避重试的执行 SQL 语句
 * @param db 数据库句柄
 * @param sql 待执行的 SQL
 * @return 0 成功，-1 失败
 */
int mf_db_execute_with_retry(mf_db_t* db, const char* sql);

/**
 * @brief 获取底层驱动连接池指针（兼容过渡用）
 * @param db 数据库句柄
 * @return 底层池指针
 */
void* mf_db_get_underlying_pool(mf_db_t* db);

#ifdef __cplusplus
}
#endif
