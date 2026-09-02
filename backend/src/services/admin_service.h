#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @file admin_service.h
 * @brief 系统管理、初始化向导与运行状态监控服务
 */

/**
 * @brief 获取服务器系统资源负载（CPU占用率、内存使用量、Goroutine/协程及数据库连接池状态）(GET /api/admin/status)
 * @param c HTTP 上下文
 */
void system_status(csilk_ctx_t* c);

/**
 * @brief 初始化向导：首次启动配置超级管理员账户 (POST /api/admin/setup)
 * @param c HTTP 上下文
 */
void system_setup(csilk_ctx_t* c);

/**
 * @brief 注册管理后台相关的 RESTful 路由
 * @param app Csilk 应用实例
 */
void register_admin_routes(csilk_app_t* app);
