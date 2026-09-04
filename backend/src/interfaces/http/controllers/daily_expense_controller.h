/**
 * @file daily_expense_controller.h
 * @brief 日常收支记账控制器头文件 (DDD 接口层)
 */

#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @brief 分页查询日常记账明细列表 (GET /api/daily-expenses)
 */
void daily_expenses_list(csilk_ctx_t* c);

/**
 * @brief 创建日常记账记录 (POST /api/daily-expenses)
 */
void daily_expenses_create(csilk_ctx_t* c);

/**
 * @brief 更新指定日常记账记录 (PUT /api/daily-expenses/:id)
 */
void daily_expenses_update(csilk_ctx_t* c);

/**
 * @brief 删除日常记账记录 (DELETE /api/daily-expenses/:id)
 */
void daily_expenses_delete(csilk_ctx_t* c);

/**
 * @brief 按月份聚合日常支出与收入统计 (GET /api/daily-expenses/monthly)
 */
void daily_expenses_monthly(csilk_ctx_t* c);

/**
 * @brief 注册日常记账模块的所有 HTTP 路由
 */
void register_daily_expense_routes(csilk_app_t* app);
