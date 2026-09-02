#pragma once

/**
 * @file db_config.h
 * @brief 数据库配置与迁移初始化接口
 *
 * 提供数据库连接池的配置初始化与数据库迁移脚本执行接口，
 * 封装对底层通用数据库模块（common/db.h）的调用。
 */

#include "csilk/drivers/db.h"

/**
 * @brief 初始化数据库连接池配置
 *
 * 根据配置文件（config/db.json）或环境变量中的配置项创建并初始化数据库连接池。
 * 支持 SQLite 与 PostgreSQL 驱动。
 *
 * @param[out] out_pool 指向数据库连接池指针的地址，成功时被赋值为新创建的连接池实例
 *
 * @return int 状态码
 * @retval 0 初始化成功
 * @retval -1 初始化失败（配置读取失败、驱动未启用或连接池创建失败）
 *
 * @note 内存所有权：成功返回后，连接池指针 *out_pool 的所有权归调用方所有，程序退出时需负责释放。
 * @note 线程安全性：应在单线程初始化阶段调用，并发调用可能导致资源竞争。
 */
int db_config_init(csilk_db_pool_t** out_pool);

/**
 * @brief 执行数据库迁移脚本
 *
 * 针对当前连接池执行预置的 SQL 迁移脚本（SQLite 或 PostgreSQL），
 * 创建所需的数据表及初始字段。
 *
 * @param[in] pool 数据库连接池指针，不可为 NULL
 *
 * @return int 状态码
 * @retval 0 迁移执行成功
 * @retval -1 迁移执行失败（SQL 语法错误、连接中断或权限不足）
 *
 * @note 内存所有权：不改变 pool 的所有权。
 * @note 线程安全性：应在服务启动阶段单线程执行，避免并发迁移导致模式锁冲突。
 */
int db_config_run_migrations(csilk_db_pool_t* pool);
