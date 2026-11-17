/**
 * @file ai-news.ts
 * @brief AI 每日市场脉搏 API 封装
 */
import http from '@/utils/http'
import type { AiNewsReport, AiNewsListResponse } from '@/types'

/** 获取最近 N 天的 AI 资讯分析报告（轻量投影，分页） */
export async function listNewsReports(
  limit = 7,
  offset = 0,
): Promise<AiNewsListResponse> {
  const res = await http.get<AiNewsListResponse>('/ai/news', {
    params: { limit, offset },
  })
  /* 兼容旧后端（无信封/has_more）: 若返回裸数组则包一层 */
  if (Array.isArray(res)) {
    return { list: res as unknown as AiNewsReport[], has_more: false }
  }
  return res || { list: [], has_more: false }
}

/** 按需获取指定日期报告的完整正文（展开时单取 analysis/sources） */
export async function getNewsReportByDate(date: string): Promise<AiNewsReport> {
  return http.get<AiNewsReport>(`/ai/news/${date}`)
}

/** 手动触发一次 AI 分析 */
export async function triggerNewsAnalysis(): Promise<{ report_id: number; message: string }> {
  return http.post<{ report_id: number; message: string }>('/ai/news/trigger')
}
