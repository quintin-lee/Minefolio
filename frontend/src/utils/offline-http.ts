/**
 * @file 移动端离线 HTTP 代理客户端与本地存储降级处理
 * @description 拦截日常收支、交易流水、资产等核心业务的增删改请求，网络不可用时自动降级写入本地 SQLite 并入队待同步队列
 */

// frontend/src/utils/offline-http.ts
import http from './http'
import { run, persist } from '@/db/local'
import type { SqlValue } from '@/types/mobile'
import { useSyncStore } from '@/stores/sync'
import { useAuthStore } from '@/stores/auth'
import { ElMessage } from 'element-plus'

/** URL 路径片段到本地 SQLite 数据库表名的映射 */
const TABLE_BY_PATH: Record<string, string> = {
  'daily-expenses': 'daily_expenses',
  'transactions': 'transactions',
  'assets': 'assets',
}

/**
 * 根据请求 URL 解析其对应的本地数据表名
 * @param url 请求路径
 * @returns 数据库表名，若不匹配返回 null
 */
function tableNameFromUrl(url: string): string | null {
  for (const key of Object.keys(TABLE_BY_PATH)) {
    if (url.includes(`/${key}`)) return TABLE_BY_PATH[key] ?? null
  }
  return null
}

/**
 * 从 RESTful URL 路径中提取末尾资源数字 ID
 * @param url 请求路径 (如 "/assets/123")
 * @returns 资源 ID 数字，若无返回 null
 */
function extractId(url: string): number | null {
  const m = url.match(/\/(\d+)(?:\?|$)/)
  return m ? Number(m[1]) : null
}

/**
 * 执行离线本地写操作并加入待同步队列
 * @param table 目标表名
 * @param operation 操作类型 ('create' | 'update' | 'delete')
 * @param recordId 记录主键 ID
 * @param payload 记录实体数据
 */
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

/**
 * 判断错误是否属于 1001 鉴权未授权
 * @param err 捕获的异常对象
 * @returns 是否为鉴权异常
 */
function isAuthError(err: any): boolean {
  return err?.response?.data?.code === 1001
}

/**
 * 执行支持离线降级的 HTTP 网络请求
 * @description
 * 1. 优先尝试向服务端发送在线请求
 * 2. 若发生 1001 鉴权错误则立即注销并抛出
 * 3. 若网络失败且为写操作 (POST/PUT/DELETE) 并且匹配业务表，则降级写入本地 SQLite 并提示用户离线保存
 * 4. 读操作 (GET) 失败直接透传抛出
 *
 * @param method HTTP 方法 ('get' | 'post' | 'put' | 'delete')
 * @param url 接口路径
 * @param data 请求体参数 (可选)
 * @returns 请求响应数据或离线写入占位结果
 */
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

/**
 * 离线支持版 HTTP API 请求对象
 */
export const offlineApi = {
  /** 发送 GET 请求 */
  get: (url: string) => offlineRequest('get', url),
  /** 发送 POST 请求 (离线自动落库) */
  post: (url: string, data?: Record<string, unknown>) => offlineRequest('post', url, data),
  /** 发送 PUT 请求 (离线自动落库) */
  put: (url: string, data?: Record<string, unknown>) => offlineRequest('put', url, data),
  /** 发送 DELETE 请求 (离线自动落库) */
  delete: (url: string) => offlineRequest('delete', url),
}

