#pragma once

/**
 * @file db.h
 * @brief 数据库连接池生命周期、迁移执行及结果集字段类型转换工具接口
 *
 * 封装底层 csilk_db 连接池单例的初始化、PostgreSQL/SQLite 驱动适配、
 * 数据库多版本热迁移执行逻辑，以及从 JSON 结果集安全提取数值/布尔类型的辅助工具。
 */

#include "csilk/drivers/db.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/**
 * @brief 初始化全局数据库连接池单例
 *
 * 根据环境变量（MINEFOLIO_DB_DRIVER, MINEFOLIO_DB_DSN）或配置文件（config/db.json）
 * 加载数据库驱动（SQLite 或 PostgreSQL），建立连接池并配置基础 PRAGMA 优化参数（如 WAL 模式、忙时等待等）。
 *
 * @param[out] out_pool 接收新创建的数据库连接池实例指针的地址
 *
 * @return int 状态码
 * @retval 0 初始化成功
 * @retval -1 初始化失败（驱动不支持、连接无法建立或配置错误）
 *
 * @note 内存所有权：内部维护静态全局连接池单例，并通过 out_pool 输出，进程退出前应适时析构。
 * @note 线程安全性：应在进程单线程启动阶段调用一次。
 */
int db_init(csilk_db_pool_t** out_pool);

/**
 * @brief 针对当前数据库连接池执行全量与增量 SQL 数据迁移
 *
 * 自动检测当前数据库类型（SQLite 或 PostgreSQL），读取对应迁移脚本（migration.sql / migration_postgres.sql），
 * 幂等执行表结构初始化、字段追加（ALTER TABLE）、外键重构与基础数据回填。
 *
 * @param[in] pool 数据库连接池指针，不可为 NULL
 *
 * @return int 状态码
 * @retval 0 迁移执行成功
 * @retval -1 迁移失败（脚本文件未找到或 SQL 执行出错）
 *
 * @note 线程安全性：应在系统单线程启动阶段执行，避免与业务读写并发冲突。
 */
int db_run_migrations(csilk_db_pool_t* pool);

/**
 * @brief 获取已初始化的全局数据库连接池单例指针
 *
 * @return csilk_db_pool_t* 全局连接池指针；若未初始化则返回 NULL
 *
 * @note 内存所有权：返回内部单例指针，严禁由外部调用者随意释放。
 * @note 线程安全性：只读访问全局指针，初始化完成后线程安全。
 */
csilk_db_pool_t* db_get_pool(void);

/**
 * @brief 查询当前数据库驱动是否为 PostgreSQL
 *
 * @return int 驱动标识
 * @retval 1 当前运行于 PostgreSQL 模式
 * @retval 0 当前运行于 SQLite 模式
 *
 * @note 线程安全性：只读访问，线程安全。
 */
int db_is_postgres(void);

/**
 * @brief 从查询结果行的 JSON 对象中安全提取浮点数值
 *
 * 底层 csilk_db_query_json() 将数据库所有列默认映射为 JSON 字符串节点（无类型自动推导），
 * 若直接调用 csilk_json_get_number() 会对字符串节点返回 0.0。
 * 本函数支持透明解析 number、boolean 以及文本型数值（如 "123.45"、"true"、"false"）。
 *
 * @param[in] obj 查询结果行 JSON 对象指针
 * @param[in] key 列名/字段键名
 *
 * @return double 解析得到的浮点数值；若字段不存在或为空则返回 0.0
 *
 * @note 线程安全性：纯只读解析，线程安全。
 */
static inline double
db_get_num(const csilk_json_t* obj, const char* key)
{
    const csilk_json_t* v = csilk_json_get(obj, key);
    if (!v) {
        return 0.0;
    }
    if (csilk_json_is_number(v)) {
        return csilk_json_number_value(v);
    }
    if (csilk_json_is_bool(v)) {
        return csilk_json_bool_value(v) ? 1.0 : 0.0;
    }
    if (csilk_json_is_string(v)) {
        const char* s = csilk_json_string_value(v);
        if (!s) {
            return 0.0;
        }
        if (strcasecmp(s, "true") == 0 || strcasecmp(s, "t") == 0) {
            return 1.0;
        }
        if (strcasecmp(s, "false") == 0 || strcasecmp(s, "f") == 0) {
            return 0.0;
        }
        return atof(s);
    }
    return 0.0;
}

/**
 * @brief 从查询结果行的 JSON 对象中安全提取 64 位整型数值
 *
 * @param[in] obj 查询结果行 JSON 对象指针
 * @param[in] key 列名/字段键名
 *
 * @return int64_t 解析得到的整数；若字段不存在或为空则返回 0
 *
 * @note 线程安全性：纯只读解析，线程安全。
 */
static inline int64_t
db_get_int(const csilk_json_t* obj, const char* key)
{
    return (int64_t)db_get_num(obj, key);
}

/**
 * @brief 从查询结果行的 JSON 对象中安全提取布尔值
 *
 * 支持识别 JSON 布尔节点、数值非 0、以及文本字符串 "true"/"t"/"1"（返回 1）与 "false"/"f"/"0"（返回 0）。
 *
 * @param[in] obj 查询结果行 JSON 对象指针
 * @param[in] key 列名/字段键名
 *
 * @return int 布尔值（1 表示 true，0 表示 false）
 *
 * @note 线程安全性：纯只读解析，线程安全。
 */
static inline int
db_get_bool(const csilk_json_t* obj, const char* key)
{
    const csilk_json_t* v = csilk_json_get(obj, key);
    if (!v) {
        return 0;
    }
    if (csilk_json_is_bool(v)) {
        return csilk_json_bool_value(v) ? 1 : 0;
    }
    if (csilk_json_is_number(v)) {
        return csilk_json_number_value(v) != 0.0 ? 1 : 0;
    }
    if (csilk_json_is_string(v)) {
        const char* s = csilk_json_string_value(v);
        if (!s) {
            return 0;
        }
        if (strcasecmp(s, "true") == 0 || strcasecmp(s, "t") == 0 || strcasecmp(s, "1") == 0) {
            return 1;
        }
        if (strcasecmp(s, "false") == 0 || strcasecmp(s, "f") == 0 || strcasecmp(s, "0") == 0) {
            return 0;
        }
        return atof(s) != 0.0 ? 1 : 0;
    }
    return 0;
}
