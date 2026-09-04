#pragma once

/**
 * @file statement.h
 * @brief 预编译语句、强类型参数绑定与游标结果集抽象
 */

#include "infrastructure/database/database.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct csilk_json_s csilk_json_t;

/**
 * @brief 在数据库连接上创建预编译语句
 * @param db 数据库句柄
 * @param sql 参数化 SQL 语句（统一使用 '?' 占位符）
 * @param out_stmt 输出语句句柄
 * @return 0 成功，-1 失败
 */
int mf_stmt_prepare(mf_db_t* db, const char* sql, mf_stmt_t** out_stmt);

/**
 * @brief 关闭并销毁预编译语句
 * @param stmt 语句句柄
 */
void mf_stmt_close(mf_stmt_t* stmt);

/**
 * @brief 重置语句与已绑定的参数
 * @param stmt 语句句柄
 */
void mf_stmt_reset(mf_stmt_t* stmt);

/* 强类型参数绑定 (index 为 1-based) */
int mf_stmt_bind_int64(mf_stmt_t* stmt, int index, int64_t val);
int mf_stmt_bind_double(mf_stmt_t* stmt, int index, double val);
int mf_stmt_bind_text(mf_stmt_t* stmt, int index, const char* text);
int mf_stmt_bind_bool(mf_stmt_t* stmt, int index, bool val);
int mf_stmt_bind_null(mf_stmt_t* stmt, int index);

/**
 * @brief 执行 DML 语句 (INSERT, UPDATE, DELETE)
 * @param stmt 语句句柄
 * @param out_affected_rows 可选接收受影响行数的指针
 * @return 0 成功，-1 失败
 */
int mf_stmt_execute(mf_stmt_t* stmt, int64_t* out_affected_rows);

/**
 * @brief 执行查询并返回游标结果集
 * @param stmt 语句句柄
 * @param out_result 输出游标结果集
 * @return 0 成功，-1 失败
 */
int mf_stmt_query(mf_stmt_t* stmt, mf_result_t** out_result);

/* 结果集游标遍历接口 */
bool        mf_result_next(mf_result_t* res);
int         mf_result_column_count(mf_result_t* res);
const char* mf_result_column_name(mf_result_t* res, int col_index);
int64_t     mf_result_get_int64(mf_result_t* res, const char* col_name);
double      mf_result_get_double(mf_result_t* res, const char* col_name);
const char* mf_result_get_text(mf_result_t* res, const char* col_name);
bool        mf_result_get_bool(mf_result_t* res, const char* col_name);
bool        mf_result_is_null(mf_result_t* res, const char* col_name);

/**
 * @brief 释放结果集游标及行内存
 * @param res 结果集指针
 */
void mf_result_free(mf_result_t* res);

/**
 * @brief 将结果集映射为 csilk_json_t* 数组（向后兼容桥接）
 * @param res 结果集指针
 * @return JSON 数组指针，需通过 csilk_json_free 释放
 */
csilk_json_t* mf_result_to_json(mf_result_t* res);

#ifdef __cplusplus
}
#endif
