/**
 * @file report_controller.h
 * @brief 统计报表、持仓分析与仪表盘数据控制器头文件 (DDD 接口层)
 *
 * 路由处理器均为 csilk 处理函数（void f(csilk_ctx_t*)），由
 * register_report_routes() 注册；参数解析与响应封装在 .c 中完成。
 */

#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @brief 注册统计报表与数据看板相关的所有 HTTP 路由
 */
void register_report_routes(csilk_app_t* app);
