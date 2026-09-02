/**
 * @file 现金流日历与收支排期 API 接口模块
 * @description 提供固定收支排期计划的增删改查、月度现金流日历预测以及实际到账确认等功能
 */

import http from '@/utils/http'
import type { CashflowSchedule, MonthlyCashflowSummary } from '@/types'

/**
 * 现金流排期与日历 API 服务对象
 */
export const cashflowApi = {
  /**
   * 获取所有现金流排期计划列表 (如每月工资、房租、定期分红、订阅扣费等)
   * @route GET /api/cashflow/schedules
   * @returns 现金流排期计划列表
   */
  listSchedules: () =>
    http.get<CashflowSchedule[], CashflowSchedule[]>('/cashflow/schedules'),

  /**
   * 创建新的现金流排期计划
   * @route POST /api/cashflow/schedules
   * @param data 现金流计划数据
   * @param data.name 计划名称
   * @param data.flow_type 流水方向 ('income' 收入 | 'expense' 支出)
   * @param data.amount 预期金额
   * @param data.frequency 周期频率 ('daily' | 'weekly' | 'monthly' | 'yearly' 等)
   * @param data.day_of_month 每月第几天扣/入款 (1-31)
   * @param data.start_date 起始日期
   * @param data.end_date 截止日期 (可选)
   * @param data.target_asset_id 目标关联资产 ID
   * @param data.category_id 对应分类 ID
   * @returns 创建成功后的新排期 ID
   */
  createSchedule: (data: Partial<CashflowSchedule>) =>
    http.post<{ id: number }, { id: number }>('/cashflow/schedules', data),

  /**
   * 获取指定 ID 的现金流排期详情
   * @route GET /api/cashflow/schedules/:id
   * @param id 排期计划 ID
   * @returns 现金流排期计划实体
   */
  getSchedule: (id: number) =>
    http.get<CashflowSchedule, CashflowSchedule>(`/cashflow/schedules/${id}`),

  /**
   * 更新现金流排期计划
   * @route PUT /api/cashflow/schedules/:id
   * @param id 排期计划 ID
   * @param data 更新的数据载荷
   * @returns 空响应
   */
  updateSchedule: (id: number, data: Partial<CashflowSchedule>) =>
    http.put<void, void>(`/cashflow/schedules/${id}`, data),

  /**
   * 删除现金流排期计划
   * @route DELETE /api/cashflow/schedules/:id
   * @param id 排期计划 ID
   * @returns 空响应
   */
  deleteSchedule: (id: number) =>
    http.delete<void, void>(`/cashflow/schedules/${id}`),

  /**
   * 获取指定年月的现金流日历预测及汇总 (包含每日收支预测明细与结余推演)
   * @route GET /api/cashflow/calendar
   * @param year 年份 (如 2026)
   * @param month 月份 (1-12)
   * @returns 月度现金流汇总与日历详情
   */
  getCalendar: (year?: number, month?: number) =>
    http.get<MonthlyCashflowSummary, MonthlyCashflowSummary>('/cashflow/calendar', {
      params: { year, month }
    }),

  /**
   * 确认一笔排期收入/支出实际发生，自动创建真实交易记录并更新资产余额
   * @route POST /api/cashflow/confirm
   * @param data 确认入账数据载荷
   * @param data.target_asset_id 入账目标资产 ID
   * @param data.source_asset_id 出账资金来源资产 ID (可选)
   * @param data.amount 实际发生金额
   * @param data.date 发生日期 (YYYY-MM-DD)
   * @param data.name 交易/流水名称 (可选)
   * @param data.note 备注说明 (可选)
   * @returns 创建成功的交易 ID 及金额
   */
  confirmIncome: (data: {
    target_asset_id: number
    source_asset_id?: number
    amount: number
    date: string
    name?: string
    note?: string
  }) =>
    http.post<{ transaction_id: number; amount: number }, { transaction_id: number; amount: number }>(
      '/cashflow/confirm',
      data
    )
}

