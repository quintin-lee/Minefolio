/**
 * @file 首页核心财务总览概览 API 接口模块
 * @description 提供系统主面板/仪表盘关键财务指标统计 (总资产、总负债、净资产、当月收支结余等)
 */

// frontend/src/api/summary.ts
import http from '@/utils/http'
import type { Summary } from '@/types'

/**
 * 仪表盘财务总览 API 服务对象
 */
export const summaryApi = {
  /**
   * 获取当前用户/账本的核心财务总览数据
   * @route GET /api/summary
   * @returns 财务总览摘要对象 (含总资产、总负债、净资产、当月收支等)
   */
  get: () => http.get<Summary, Summary>('/summary'),
}

