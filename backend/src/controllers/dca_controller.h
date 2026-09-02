/**
 * @file dca_controller.h
 * @brief 定投策略（Dollar-Cost Averaging）与自动计划执行控制器头文件
 *
 * 声明定期定额投资计划的创建与配置管理、周期执行记录跟踪、待办任务触发与手动确认/跳过
 * 等端点的路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 注册定投策略与计划管理相关的所有 HTTP 路由
 *
 * @details 注册包括定投计划 CRUD、状态变更、执行流水及待办确认等端点：
 *          - GET    /api/dca/plans: 获取定投计划列表
 *          - POST   /api/dca/plans: 创建新定投计划
 *          - GET    /api/dca/plans/:id: 获取定投计划详情
 *          - PUT    /api/dca/plans/:id: 更新定投计划参数
 *          - PUT    /api/dca/plans/:id/status: 更新计划运行状态 (active/paused/completed)
 *          - DELETE /api/dca/plans/:id: 删除定投计划
 *          - GET    /api/dca/plans/:id/executions: 获取指定计划的历史执行记录
 *          - GET    /api/dca/executions/pending: 获取当前用户的待执行定投任务
 *          - POST   /api/dca/executions/:id/confirm: 确认执行定投买入并入账
 *          - POST   /api/dca/executions/:id/skip: 跳过当期定投买入任务
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_dca_routes(csilk_app_t* app);
