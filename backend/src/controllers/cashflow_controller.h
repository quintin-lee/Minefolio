/**
 * @file cashflow_controller.h
 * @brief 被动现金流计划与日历预测控制器头文件
 *
 * 声明被动收入计划（股息分红、存款利息、房租租金、债券付息等）的增删改查、
 * 月度现金流日历预测（Cashflow Calendar）以及收益实际到账确认（Confirm Income）相关的 HTTP 路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 注册被动现金流计划与日历预测相关的所有 HTTP 路由
 *
 * @details 注册包括现金流计划管理、月度日历预测与收益到账确认等端点：
 *          - GET    /api/cashflow/schedules: 获取用户现金流计划列表
 *          - POST   /api/cashflow/schedules: 创建新现金流计划
 *          - GET    /api/cashflow/schedules/:id: 获取现金流计划详情
 *          - PUT    /api/cashflow/schedules/:id: 更新现金流计划
 *          - DELETE /api/cashflow/schedules/:id: 删除现金流计划
 *          - GET    /api/cashflow/calendar: 获取月度现金流日历事件预测与汇总
 *          - POST   /api/cashflow/confirm: 确认被动收入到账并自动入账生成交易
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_cashflow_routes(csilk_app_t* app);
