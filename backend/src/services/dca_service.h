#pragma once
#include "csilk/csilk.h"

/**
 * @file dca_service.h
 * @brief 定投计划 (DCA - Dollar-Cost Averaging) 策略管理与一键扣款买入执行服务
 */

/**
 * @brief 分页查询当前用户的定投计划列表 (GET /api/dca/plans)
 * 包含累计定投期数、已投入本金、当前浮动盈亏等统计指标
 * @param c HTTP 上下文
 */
void dca_service_list_plans(csilk_ctx_t* c);

/**
 * @brief 创建新的定投计划 (POST /api/dca/plans)
 * @param c HTTP 上下文
 */
void dca_service_create_plan(csilk_ctx_t* c);

/**
 * @brief 获取单个定投计划详情 (GET /api/dca/plans/:id)
 * @param c HTTP 上下文
 */
void dca_service_get_plan(csilk_ctx_t* c);

/**
 * @brief 更新定投计划参数 (PUT /api/dca/plans/:id)
 * @param c HTTP 上下文
 */
void dca_service_update_plan(csilk_ctx_t* c);

/**
 * @brief 切换定投计划的启用/暂停状态 (PATCH /api/dca/plans/:id/status)
 * @param c HTTP 上下文
 */
void dca_service_set_plan_status(csilk_ctx_t* c);

/**
 * @brief 删除定投计划及其关联待执行计划 (DELETE /api/dca/plans/:id)
 * @param c HTTP 上下文
 */
void dca_service_delete_plan(csilk_ctx_t* c);

/**
 * @brief 分页查询定投执行历史明细 (GET /api/dca/executions)
 * @param c HTTP 上下文
 */
void dca_service_list_executions(csilk_ctx_t* c);

/**
 * @brief 获取当前所有待执行 (pending) 的定投任务 (GET /api/dca/executions/pending)
 * @param c HTTP 上下文
 */
void dca_service_list_pending_executions(csilk_ctx_t* c);

/**
 * @brief 确认执行定投买入 (POST /api/dca/executions/:id/confirm)
 * 自动完成：资金账户余额扣减、投资标的持仓/成本均摊更新、交易记录插入
 * @param c HTTP 上下文
 */
void dca_service_confirm_execution(csilk_ctx_t* c);

/**
 * @brief 跳过本次定投执行任务 (POST /api/dca/executions/:id/skip)
 * @param c HTTP 上下文
 */
void dca_service_skip_execution(csilk_ctx_t* c);
