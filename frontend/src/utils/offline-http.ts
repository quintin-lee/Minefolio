/**
 * @file 移动端离线 HTTP 代理客户端与本地存储降级处理
 * @description 拦截日常收支、交易流水、资产等核心业务的增删改请求，网络不可用时自动降级写入本地 SQLite 并入队待同步队列
 */

// frontend/src/utils/offline-http.ts
import http from './http'
import { query, run, rowsFrom, persist } from '@/db/local'
import { TABLE_COLUMNS } from '@/db/schema'
import type { SqlValue } from '@/types/mobile'
import { useSyncStore } from '@/stores/sync'
import { useAuthStore } from '@/stores/auth'
import { ElMessage } from 'element-plus'

/** URL 路径片段到本地 SQLite 数据库表名的映射 */
const TABLE_BY_PATH: Record<string, string> = {
  'daily-expenses': 'daily_expenses',
  'transactions': 'transactions',
  'assets': 'assets',
  'accounts': 'accounts',
}

/**
 * 根据请求 URL 解析其对应的本地数据表名
 * 仅匹配集合路径或其数字 ID 子资源（如 `/daily-expenses`、`/daily-expenses/12`），
 * 排除 `/daily-expenses/import`、`/transactions/monthly` 等子动作接口。
 * @param url 请求路径
 * @returns 数据库表名，若不匹配返回 null
 */
function tableNameFromUrl(url: string): string | null {
  const m = url.match(/^(?:\/api)?\/(daily-expenses|transactions|assets|accounts)(?:\/\d+)?(?:\?|$)/)
  if (!m || !m[1]) return null
  return TABLE_BY_PATH[m[1]] ?? null
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
 * 生成一个不与本地现有行冲突的记录 ID (基于时间戳，遇冲突自增)。
 * 离线创建的本地行、同步队列 record_id、外键引用必须使用同一 ID，
 * 否则同步完成后的 ID 重映射会失配并产生重复记录。
 * @param table 目标表名
 * @returns 可用的本地记录 ID
 */
function nextLocalId(table: string): number {
  let candidate = Date.now()
  while (rowsFrom(query(`SELECT id FROM ${table} WHERE id = ? LIMIT 1`, [candidate])).length > 0) {
    candidate += 1
  }
  return candidate
}

/**
 * 执行离线本地写操作并加入待同步队列
 * @param table 目标表名
 * @param operation 操作类型 ('create' | 'update' | 'delete')
 * @param recordId 记录主键 ID
 * @param payload 记录实体数据
 */
async function writeLocal(table: string, operation: 'create' | 'update' | 'delete', recordId: number, payload: Record<string, unknown>): Promise<void> {
  const queuePayload: Record<string, unknown> = { ...payload }
  if (queuePayload.ledger_id === undefined) {
    const activeLedger = Number(localStorage.getItem('minefolio_active_ledger_id') || 0)
    if (activeLedger > 0) {
      queuePayload.ledger_id = activeLedger
    }
  }
  if (operation === 'delete') {
    run(`UPDATE ${table} SET __deleted = 1 WHERE id = ?`, [recordId])
    persist()
    useSyncStore().enqueue(table, recordId, operation, queuePayload)
    return
  }
  const allowed = TABLE_COLUMNS[table]
  const cols = Object.keys(queuePayload).filter(
    (k) => k !== '__deleted' && k !== 'id' && (!allowed || allowed.includes(k)) && queuePayload[k] !== undefined
  )
  if (cols.length === 0) {
    // 无有效字段可写（例如仅带 id 或全为非持久化扩展字段），只入队不做本地写入
    persist()
    useSyncStore().enqueue(table, recordId, operation, queuePayload)
    return
  }
  const toSqlVal = (v: unknown): SqlValue => (typeof v === 'boolean' ? (v ? 1 : 0) : (v as SqlValue))
  const placeholders = cols.map(() => '?').join(', ')
  const updates = cols.map((c) => `${c} = ?`).join(', ')
  run(
    `INSERT INTO ${table} (${cols.join(', ')}, id, __deleted) VALUES (${placeholders}, ?, 0)
     ON CONFLICT(id) DO UPDATE SET ${updates}, __deleted = 0`,
    [...cols.map((c) => toSqlVal(queuePayload[c])), recordId, ...cols.map((c) => toSqlVal(queuePayload[c]))]
  )
  persist()
  useSyncStore().enqueue(table, recordId, operation, queuePayload)
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
 * 判断异常是否属于无服务端响应的纯网络/超时故障（可安全降级本地离线保存）；
 * 若存在 err.response（包括 HTTP 4xx/5xx 或业务错误信封），必须直接向上抛出，
 * 避免将权限拒绝、参数校验失败或服务端报错误伪装为离线写入成功。
 */
export function isNetworkError(err: unknown): boolean {
  if (!err || typeof err !== 'object') return false
  const e = err as { response?: unknown; message?: unknown }
  if (e.response !== undefined && e.response !== null) return false
  if (err instanceof TypeError && String(e.message || '').includes('is not a function')) {
    return false
  }
  return true
}

/**
 * 执行支持离线降级的 HTTP 网络请求
 * @description
 * 1. 优先尝试向服务端发送在线请求
 * 2. 若发生 1001 鉴权错误则立即注销并抛出
 * 3. 若发生网络/超时故障且为写操作 (POST/PUT/DELETE) 并且匹配业务表，则降级写入本地 SQLite 并提示用户离线保存
 * 4. 读操作 (GET) 及 HTTP 4xx/5xx / 业务错误直接透传抛出
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
      const res = await http.request<unknown>({ method, url, data })
      return res
    } catch (err: any) {
      if (isAuthError(err)) {
        await useAuthStore().logout?.()
        throw err
      }
      // 仅对网络故障下的写操作做离线落库；读操作与服务端响应错误(4xx/5xx/业务码)直接抛出
      if (method === 'get' || !isNetworkError(err)) throw err
      const recordId = id ?? (data?.id as number) ?? nextLocalId(table)
      await writeLocal(table, operation, recordId, data ?? {})
      ElMessage.success('已离线保存，联网后自动同步')
      return { offline: true, id: recordId }
    }
  }
  // 非业务表请求（如 summary、reports）→ 直接透传
  return http.request<unknown>({ method, url, data })
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

