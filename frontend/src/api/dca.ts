/**
 * @file 定投计划 (DCA - Dollar Cost Averaging) API 接口模块
 * @description 提供定期定额投资计划的配置管理、定投执行日志追踪、待执行任务确认与跳过等接口
 */

import http from '@/utils/http'
import type { DcaPlan, DcaExecution } from '@/types'

/**
 * 定投管理 API 服务对象
 */
export const dcaApi = {
  /**
   * 获取所有定投计划列表
   * @route GET /api/dca/plans
   * @returns 定投计划对象数组
   */
  listPlans: () =>
    http.get<DcaPlan[], DcaPlan[]>('/dca/plans'),

  /**
   * 创建新的定投计划
   * @route POST /api/dca/plans
   * @param data 定投计划配置数据
   * @param data.name 计划名称 (如 '沪深300周定投')
   * @param data.target_asset_id 目标买入资产 ID (基金/股票/加密货币等)
   * @param data.funding_asset_id 资金扣款账户 ID (如银行卡/余额)
   * @param data.amount 每期定投金额
   * @param data.frequency 定投周期 ('daily' | 'weekly' | 'biweekly' | 'monthly')
   * @param data.day_of_period 周期内的指定日 (如周几 1-7 或每月几号 1-31)
   * @param data.start_date 起始生效日期
   * @param data.end_date 截止日期 (可选)
   * @returns 创建成功后的新计划 ID
   */
  createPlan: (data: Partial<DcaPlan>) =>
    http.post<{ id: number }, { id: number }>('/dca/plans', data),

  /**
   * 获取指定 ID 的定投计划详情
   * @route GET /api/dca/plans/:id
   * @param id 定投计划 ID
   * @returns 定投计划实体
   */
  getPlan: (id: number) =>
    http.get<DcaPlan, DcaPlan>(`/dca/plans/${id}`),

  /**
   * 更新定投计划配置
   * @route PUT /api/dca/plans/:id
   * @param id 定投计划 ID
   * @param data 更新的数据载荷
   * @returns 空响应
   */
  updatePlan: (id: number, data: Partial<DcaPlan>) =>
    http.put<void, void>(`/dca/plans/${id}`, data),

  /**
   * 设置定投计划的运行状态 (启用/暂停/已完成)
   * @route PUT /api/dca/plans/:id/status
   * @param id 定投计划 ID
   * @param status 目标状态 ('active': 活跃运行, 'paused': 暂停定投, 'completed': 归档结束)
   * @returns 空响应
   */
  setPlanStatus: (id: number, status: 'active' | 'paused' | 'completed') =>
    http.put<void, void>(`/dca/plans/${id}/status`, { status }),

  /**
   * 删除定投计划
   * @route DELETE /api/dca/plans/:id
   * @param id 定投计划 ID
   * @returns 空响应
   */
  deletePlan: (id: number) =>
    http.delete<void, void>(`/dca/plans/${id}`),

  /**
   * 获取指定定投计划的历史执行记录列表
   * @route GET /api/dca/plans/:planId/executions
   * @param planId 定投计划 ID
   * @returns 该计划的定投执行记录数组
   */
  listExecutions: (planId: number) =>
    http.get<DcaExecution[], DcaExecution[]>(`/dca/plans/${planId}/executions`),

  /**
   * 获取所有当前待确认/待执行的定投任务列表
   * @route GET /api/dca/executions/pending
   * @returns 待执行的定投记录数组
   */
  listPendingExecutions: () =>
    http.get<DcaExecution[], DcaExecution[]>('/dca/executions/pending'),

  /**
   * 确认执行一笔定投买入 (可填入实际买入价格与实际扣款金额，自动生成交易记录与持仓份额变动)
   * @route POST /api/dca/executions/:id/confirm
   * @param id 执行记录 ID
   * @param data 实际成交数据
   * @param data.actual_amount 实际成交总金额 (可选，默认按计划金额)
   * @param data.executed_price 实际成交单价 (可选)
   * @returns 执行结果 (包含生成的交易 ID、成交金额、单价与新增份额)
   */
  confirmExecution: (
    id: number,
    data?: { actual_amount?: number; executed_price?: number }
  ) =>
    http.post<
      { transaction_id: number; actual_amount: number; executed_price: number; executed_quantity: number },
      { transaction_id: number; actual_amount: number; executed_price: number; executed_quantity: number }
    >(`/dca/executions/${id}/confirm`, data || {}),

  /**
   * 跳过本期定投计划执行
   * @route POST /api/dca/executions/:id/skip
   * @param id 执行记录 ID
   * @returns 空响应
   */
  skipExecution: (id: number) =>
    http.post<void, void>(`/dca/executions/${id}/skip`)
}

