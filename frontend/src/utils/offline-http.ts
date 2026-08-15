// frontend/src/utils/offline-http.ts
import http from './http'
import { run, persist } from '@/db/local'
import type { SqlValue } from '@/types/mobile'
import { useSyncStore } from '@/stores/sync'
import { useAuthStore } from '@/stores/auth'
import { ElMessage } from 'element-plus'

const TABLE_BY_PATH: Record<string, string> = {
  'daily-expenses': 'daily_expenses',
  'transactions': 'transactions',
  'assets': 'assets',
}

function tableNameFromUrl(url: string): string | null {
  for (const key of Object.keys(TABLE_BY_PATH)) {
    if (url.includes(`/${key}`)) return TABLE_BY_PATH[key] ?? null
  }
  return null
}

function extractId(url: string): number | null {
  const m = url.match(/\/(\d+)(?:\?|$)/)
  return m ? Number(m[1]) : null
}

async function writeLocal(table: string, operation: 'create' | 'update' | 'delete', recordId: number, payload: Record<string, unknown>): Promise<void> {
  if (operation === 'delete') {
    run(`UPDATE ${table} SET __deleted = 1 WHERE id = ?`, [recordId])
  } else {
    const cols = Object.keys(payload).filter((k) => k !== '__deleted')
    const placeholders = cols.map(() => '?').join(', ')
    const updates = cols.map((c) => `${c} = ?`).join(', ')
    run(
      `INSERT INTO ${table} (${cols.join(', ')}, __deleted) VALUES (${placeholders}, 0)
       ON CONFLICT(id) DO UPDATE SET ${updates}, __deleted = 0`,
      [...cols.map((c) => payload[c] as SqlValue), ...cols.map((c) => payload[c] as SqlValue)]
    )
  }
  persist()
  useSyncStore().enqueue(table, recordId, operation, payload)
}

function isAuthError(err: any): boolean {
  return err?.response?.data?.code === 1001
}

export async function offlineRequest(method: 'get' | 'post' | 'put' | 'delete', url: string, data?: Record<string, unknown>): Promise<any> {
  const table = tableNameFromUrl(url)
  if (table) {
    const id = extractId(url)
    const operation: 'create' | 'update' | 'delete' =
      method === 'post' ? 'create' : method === 'put' ? 'update' : 'delete'
    try {
      const res = await http({ method, url, data } as any)
      return res
    } catch (err: any) {
      if (isAuthError(err)) {
        useAuthStore().logout?.()
        throw err
      }
      // 只对写操作做离线落库；失败的读请求直接透传错误（由 pullSync 负责离线读）
      if (method === 'get') throw err
      const recordId = id ?? (data?.id as number) ?? Date.now()
      await writeLocal(table, operation, recordId, data ?? {})
      ElMessage.success('已离线保存，联网后自动同步')
      return { offline: true, id: recordId }
    }
  }
  // 非业务表请求（如 summary、reports）→ 直接透传
  return http({ method, url, data } as any)
}

export const offlineApi = {
  get: (url: string) => offlineRequest('get', url),
  post: (url: string, data?: Record<string, unknown>) => offlineRequest('post', url, data),
  put: (url: string, data?: Record<string, unknown>) => offlineRequest('put', url, data),
  delete: (url: string) => offlineRequest('delete', url),
}
