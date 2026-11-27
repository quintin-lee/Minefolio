/**
 * @file import_export_controller.h
 * @brief 财务数据导入与导出控制器头文件 (DDD 接口层)
 */

#pragma once
#include "csilk/app/app.h"

/**
 * @brief 注册数据导入与导出相关的所有 HTTP 路由
 */
void register_import_export_routes(csilk_app_t* app);
