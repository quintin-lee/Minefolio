/**
 * @file db_config.c
 * @brief 数据库配置与迁移初始化实现
 *
 * 实现了配置层的数据库连接池初始化和数据库表迁移调用，
 * 作为应用启动配置流程与底层 common/db 模块之间的适配层。
 */

#include "config/db_config.h"
#include "common/db.h"
#include <stdio.h>

/**
 * @brief 初始化数据库连接池配置
 *
 * 转发调用通用数据库模块的 db_init 函数以初始化连接池。
 *
 * @param[out] out_pool 输出参数，用于接收初始化后的连接池指针
 * @return int 0 表示成功，-1 表示失败
 */
int
db_config_init(csilk_db_pool_t** out_pool)
{
    return db_init(out_pool);
}

/**
 * @brief 执行数据库迁移脚本
 *
 * 转发调用通用数据库模块的 db_run_migrations 函数执行数据库脚本。
 *
 * @param[in] pool 数据库连接池指针
 * @return int 0 表示成功，-1 表示失败
 */
int
db_config_run_migrations(csilk_db_pool_t* pool)
{
    return db_run_migrations(pool);
}
