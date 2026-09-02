#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @file daily_expense_service.h
 * @brief 日常收支记账（支出/收入）、分类统计与账户余额联动业务逻辑服务
 */

/**
 * @brief 分页查询日常收支明细列表 (GET /api/daily-expenses)
 * 支持按日期区间 (start_date/end_date)、分类 (category_id)、收支类型 (expense/income)、关键字搜索与排序
 * @param c HTTP 上下文
 */
void daily_expenses_list(csilk_ctx_t* c);

/**
 * @brief 录入单笔或批量日常收支记录 (POST /api/daily-expenses)
 * 自动联动扣减或增加关联资产账户余额并写入审计流水
 * @param c HTTP 上下文
 */
void daily_expenses_create(csilk_ctx_t* c);

/**
 * @brief 修改已有的日常收支记录 (PUT /api/daily-expenses/:id)
 * 自动反向回滚旧金额在资产账户中的增减量，并施加新金额差值
 * @param c HTTP 上下文
 */
void daily_expenses_update(csilk_ctx_t* c);

/**
 * @brief 删除日常收支记录 (DELETE /api/daily-expenses/:id)
 * 自动回滚对应的资产账户余额
 * @param c HTTP 上下文
 */
void daily_expenses_delete(csilk_ctx_t* c);

/**
 * @brief 按月份聚合日常收支总额与分类明细 (GET /api/daily-expenses/monthly)
 * @param c HTTP 上下文（支持 month 查询参数，如 "2026-08"）
 */
void daily_expenses_monthly(csilk_ctx_t* c);

/**
 * @brief 注册日常收支模块的 RESTful 路由
 * @param app Csilk 应用实例
 */
void register_daily_expense_routes(csilk_app_t* app);
