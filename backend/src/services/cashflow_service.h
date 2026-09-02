#pragma once
#include "csilk/csilk.h"

/**
 * @file cashflow_service.h
 * @brief 周期性现金流计划（工资、房租、分红、利息、分期还款）与现金流日历预测服务
 */

/**
 * @brief 分页查询用户的周期性现金流计划列表 (GET /api/cashflow/schedules)
 * @param c HTTP 上下文
 */
void cashflow_service_list_schedules(csilk_ctx_t* c);

/**
 * @brief 创建新的周期性现金流计划 (POST /api/cashflow/schedules)
 * @param c HTTP 上下文
 */
void cashflow_service_create_schedule(csilk_ctx_t* c);

/**
 * @brief 获取单个现金流计划详情 (GET /api/cashflow/schedules/:id)
 * @param c HTTP 上下文
 */
void cashflow_service_get_schedule(csilk_ctx_t* c);

/**
 * @brief 更新现金流计划配置 (PUT /api/cashflow/schedules/:id)
 * @param c HTTP 上下文
 */
void cashflow_service_update_schedule(csilk_ctx_t* c);

/**
 * @brief 删除现金流计划 (DELETE /api/cashflow/schedules/:id)
 * @param c HTTP 上下文
 */
void cashflow_service_delete_schedule(csilk_ctx_t* c);

/**
 * @brief 获取指定月份的现金流日历预测及每日事件分布 (GET /api/cashflow/calendar)
 * 聚合周期性计划预测金额与当月已实际确认的真实交易金额
 * @param c HTTP 上下文（支持 year, month 查询参数）
 */
void cashflow_service_get_calendar(csilk_ctx_t* c);

/**
 * @brief 确认现金流到账/扣款 (POST /api/cashflow/confirm)
 * 自动生成日常收支或投资交易，并联动更新目标资产账户余额
 * @param c HTTP 上下文
 */
void cashflow_service_confirm(csilk_ctx_t* c);
