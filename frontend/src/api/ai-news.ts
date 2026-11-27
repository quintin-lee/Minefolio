/**
 * @file ai-news.ts
 * @brief AI 每日市场脉搏 API 封装
 */
import http from '@/utils/http'
import type { AiNewsReport, AiNewsListResponse } from '@/types'

function buildNewsRequestConfig(ledgerId?: number, baseParams?: Record<string, number>) {
  const params: Record<string, number> = { ...baseParams }
  const config: { params: Record<string, number>; headers?: Record<string, string> } = { params }
  if (ledgerId && ledgerId > 0) {
    params.ledger_id = ledgerId
    config.headers = { 'X-Ledger-Id': String(ledgerId) }
  }
  return config
}

/** 获取 AI 资讯分析报告（轻量投影，支持 page/page_size 分页） */
export async function listNewsReports(
  page = 1,
  pageSize = 10,
  ledgerId?: number,
): Promise<AiNewsListResponse> {
  const res = await http.get<AiNewsListResponse | AiNewsReport[]>(
    '/ai/news',
    buildNewsRequestConfig(ledgerId, { page, page_size: pageSize }),
  )
  /* 兼容旧后端（无信封/has_more）: 若返回裸数组则包一层 */
  if (Array.isArray(res)) {
    return { list: res, total: res.length, page, page_size: pageSize, has_more: false }
  }
  const list = Array.isArray(res?.list) ? res.list : []
  const total = typeof res?.total === 'number' ? res.total : list.length
  return {
    list,
    total,
    page: typeof res?.page === 'number' ? res.page : page,
    page_size: typeof res?.page_size === 'number' ? res.page_size : pageSize,
    has_more: Boolean(res?.has_more),
  }
}

/** 按需获取指定日期报告的完整正文（展开时单取 analysis/sources） */
export async function getNewsReportByDate(date: string, ledgerId?: number): Promise<AiNewsReport> {
  return http.get<AiNewsReport>(`/ai/news/${date}`, buildNewsRequestConfig(ledgerId))
}

/** 手动触发一次 AI 分析 */
export async function triggerNewsAnalysis(ledgerId?: number): Promise<{ report_id: number; message: string }> {
  return http.post<{ report_id: number; message: string }>('/ai/news/trigger', null, buildNewsRequestConfig(ledgerId))
}
