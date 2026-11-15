/**
 * @file ai-news.ts
 * @brief AI 每日市场脉搏 API 封装
 */
import http from '@/utils/http'
import type { AiNewsReport } from '@/types'

/** 获取最近 N 天的 AI 资讯分析报告 */
export async function listNewsReports(limit = 7): Promise<AiNewsReport[]> {
  const res = await http.get<AiNewsReport[]>('/ai/news', { params: { limit } })
  return res || []
}

/** 获取指定日期的报告 */
export async function getNewsReportByDate(date: string): Promise<AiNewsReport> {
  return http.get<AiNewsReport>(`/ai/news/${date}`)
}

/** 手动触发一次 AI 分析 */
export async function triggerNewsAnalysis(): Promise<{ report_id: number; message: string }> {
  return http.post<{ report_id: number; message: string }>('/ai/news/trigger')
}
