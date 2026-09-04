/**
 * @file admin_controller.h
 * @brief 系统管理与初始引导控制器头文件 (DDD 接口层)
 */

#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @brief 获取系统初始化状态 (GET /api/system/status)
 */
void system_status(csilk_ctx_t* c);

/**
 * @brief 系统首次初始化设置引导 (POST /api/system/setup)
 */
void system_setup(csilk_ctx_t* c);

/**
 * @brief 注册系统管理相关 RESTful 路由
 */
void register_admin_routes(csilk_app_t* app);
