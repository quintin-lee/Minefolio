/**
 * @file 移动端离线同步引擎（单例）
 * @description 唯一执行者：队列持久化在本地 SQLite sync_queue，内存状态为其镜像；
 *              统一处理入队、推送、拉取、冲突手动确认、ID 重映射与状态事件派发。
 */

import { query, run, rowsFrom, persist, resetLocalDb } from '@/db/local'
import { TABLE_COLUMNS } from '@/db/schema'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import { accountsApi } from '@/api/accounts'
import { categoriesApi } from '@/api/categories'
import { tagsApi } from '@/api/tags'
import type { SyncQueueItem, SqlValue } from '@/types/mobile'

export type SyncOperation = 'create' | 'update' | 'delete'

export interface SyncApi {
  create(payload: Record<string, unknown>, config?: any): Promise<any>
  update(id: number, payload: Record<string, unknown>, config?: any): Promise<any>
  delete(id: number, config?: any): Promise<any>
  list(params?: any): Promise<any>
}

export interface SyncTableDef {
  table: string
  api: SyncApi
}

export interface RemapHandlers {
  onAssetRemap(oldId: number, newId: number, afterQueueId: number): void
  onAccountRemap(oldId: number, newId: number, afterQueueId: number): void
  onCategoryRemap?(oldId: number, newId: number, afterQueueId: number): void
  onTagRemap?(oldId: number, newId: number, afterQueueId: number): void
}

export interface PushError {
  queueId: number
  table: string
  message: string
}

export interface SyncStateSnapshot {
  queue: SyncQueueItem[]
  syncing: boolean
  lastSyncAt: string | null
  pushErrors: PushError[]
}

/** 后端业务错误码：1001 鉴权失效、1004 资源/操作冲突 */
const AUTH_FAIL_CODE = 1001
const CONFLICT_CODE = 1004
/** 不标冲突、直接放弃队列项并提示用户的校验/权限类业务码 */
const VALIDATION_CODES = new Set([1002, 1003, 2001])
const SYNC_STATE_EVENT = 'minefolio:sync-state-changed'

// 各 API 模块的 create/update 入参是具体 Input 类型，运行时接收宽松 payload；
// 这里保持 any 映射，避免把具体 DTO 类型强行收窄成 SyncApi 造成类型不兼容。
const API_BY_TABLE: Record<string, any> = {
  accounts: accountsApi,
  categories: categoriesApi,
  tags: tagsApi,
  assets: assetsApi,
  transactions: transactionsApi,
  daily_expenses: dailyExpensesApi,
}

/** 统一解析后端无时区后缀的 UTC 时间字符串 */
export function parseSyncTimestamp(ts: unknown): number {
  if (!ts || typeof ts !== 'string') return 0
  const trimmed = ts.trim()
  if (!trimmed) return 0
  let normalized = trimmed
  if (/^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(\.\d+)?$/.test(trimmed)) {
    normalized = trimmed.replace(' ', 'T') + 'Z'
  }
  const ms = Date.parse(normalized)
  return Number.isNaN(ms) ? 0 : ms
}

function parsePayload(raw: unknown): Record<string, unknown> {
  try {
    return JSON.parse(String(raw)) as Record<string, unknown>
  } catch {
    return {}
  }
}

function parseRemotePayload(raw: unknown): Record<string, unknown> | undefined {
  if (raw === null || raw === undefined || raw === '') return undefined
  try {
    return JSON.parse(String(raw)) as Record<string, unknown>
  } catch {
    return undefined
  }
}

/** 递归展平分类树，确保各级子分类进入本地 SQLite 且不被差异清理误删 */
function flattenCategoryTree(nodes: any[]): any[] {
  const flat: any[] = []
  const visited = new Set<number>()
  const visit = (items: any[]) => {
    for (const item of items) {
      if (!item || typeof item !== 'object') continue
      if (item.id !== undefined && item.id !== null) {
        const id = Number(item.id)
        if (visited.has(id)) continue
        visited.add(id)
      }
      const { children, ...rest } = item
      flat.push(rest)
      if (Array.isArray(children) && children.length > 0) {
        visit(children)
      }
    }
  }
  visit(nodes)
  return flat
}

export class SyncEngine {
  private static instance: SyncEngine | null = null

  private readonly registeredTables = new Map<string, SyncTableDef>()
  private remapHandlers: RemapHandlers | null = null
  private authFailHandler: (() => void) | null = null
  private queue: SyncQueueItem[] = []
  private syncing = false
  private lastSyncAt: string | null = null
  private pushErrors: PushError[] = []

  private constructor() {
    // 默认注册 6 张可写表；bootstrap 的 registerTable() 可覆盖同名定义。
    // 默认注册也保证单元测试无需 bootstrap 即可走完整流程。
    for (const [table, api] of Object.entries(API_BY_TABLE)) {
      this.registeredTables.set(table, { table, api })
    }
    // 默认重映射实现；registerRemapHandlers() 可覆盖（如由 sync.ts 注入自定义回调）
    this.remapHandlers = {
      onAssetRemap: (oldId, newId, afterQueueId) => this.remapAssetRefs(oldId, newId, afterQueueId),
      onAccountRemap: (oldId, newId, afterQueueId) => this.remapAccountRefs(oldId, newId, afterQueueId),
      onCategoryRemap: (oldId, newId, afterQueueId) => this.remapCategoryRefs(oldId, newId, afterQueueId),
      onTagRemap: (oldId, newId, afterQueueId) => this.remapTagRefs(oldId, newId, afterQueueId),
    }
  }

  /** 资产离线创建获得服务端 ID 后，重映射本地依赖行与后续队列 payload */
  private remapAssetRefs(oldId: number, newId: number, afterQueueId: number): void {
    run('UPDATE daily_expenses SET asset_id = ? WHERE asset_id = ?', [newId, oldId])
    run('UPDATE transactions SET asset_id = ? WHERE asset_id = ?', [newId, oldId])
    run('UPDATE transactions SET linked_asset_id = ? WHERE linked_asset_id = ?', [newId, oldId])
    const queued = rowsFrom(query(
      "SELECT id, table_name, payload FROM sync_queue WHERE synced = 0 AND id > ? AND table_name IN ('daily_expenses', 'transactions')",
      [afterQueueId]
    ))
    for (const q of queued) {
      const payload = parsePayload(q.payload)
      const isTx = q.table_name === 'transactions'
      let changed = false
      if (payload.asset_id === oldId) {
        payload.asset_id = newId
        changed = true
      }
      if (isTx && payload.linked_asset_id === oldId) {
        payload.linked_asset_id = newId
        changed = true
      }
      if (changed) run('UPDATE sync_queue SET payload = ? WHERE id = ?', [JSON.stringify(payload), Number(q.id)])
    }
    persist()
  }

  /** 账户离线创建获得服务端 ID 后，重映射本地 assets.account_id 与后续队列 payload */
  private remapAccountRefs(oldId: number, newId: number, afterQueueId: number): void {
    run('UPDATE assets SET account_id = ? WHERE account_id = ?', [newId, oldId])
    const queued = rowsFrom(query(
      "SELECT id, payload FROM sync_queue WHERE synced = 0 AND id > ? AND table_name = 'assets'",
      [afterQueueId]
    ))
    for (const q of queued) {
      const payload = parsePayload(q.payload)
      if (payload.account_id === oldId) {
        payload.account_id = newId
        run('UPDATE sync_queue SET payload = ? WHERE id = ?', [JSON.stringify(payload), Number(q.id)])
      }
    }
    persist()
  }

  /** 分类离线创建获得服务端 ID 后，重映射父分类引用与依赖行的 category_id */
  private remapCategoryRefs(oldId: number, newId: number, afterQueueId: number): void {
    run('UPDATE categories SET parent_id = ? WHERE parent_id = ?', [newId, oldId])
    run('UPDATE assets SET category_id = ? WHERE category_id = ?', [newId, oldId])
    run('UPDATE transactions SET category_id = ? WHERE category_id = ?', [newId, oldId])
    run('UPDATE daily_expenses SET category_id = ? WHERE category_id = ?', [newId, oldId])
    const queued = rowsFrom(query(
      "SELECT id, table_name, payload FROM sync_queue WHERE synced = 0 AND id > ? AND table_name IN ('categories', 'assets', 'transactions', 'daily_expenses')",
      [afterQueueId]
    ))
    for (const q of queued) {
      const payload = parsePayload(q.payload)
      const key = q.table_name === 'categories' ? 'parent_id' : 'category_id'
      if (payload[key] === oldId) {
        payload[key] = newId
        run('UPDATE sync_queue SET payload = ? WHERE id = ?', [JSON.stringify(payload), Number(q.id)])
      }
    }
    persist()
  }

  /** 标签离线创建获得服务端 ID 后，重映射本地关联表与后续队列 payload.tags */
  private remapTagRefs(oldId: number, newId: number, afterQueueId: number): void {
    // 复合主键冲突时忽略（同一支出已用真实 ID 关联该标签）
    run('UPDATE OR IGNORE expense_tags SET tag_id = ? WHERE tag_id = ?', [newId, oldId])
    run('DELETE FROM expense_tags WHERE tag_id = ?', [oldId])
    const queued = rowsFrom(query(
      "SELECT id, payload FROM sync_queue WHERE synced = 0 AND id > ? AND table_name = 'daily_expenses'",
      [afterQueueId]
    ))
    for (const q of queued) {
      const payload = parsePayload(q.payload)
      const tags = payload.tags
      if (Array.isArray(tags) && tags.some((t) => t === oldId)) {
        payload.tags = tags.map((t) => (t === oldId ? newId : t))
        run('UPDATE sync_queue SET payload = ? WHERE id = ?', [JSON.stringify(payload), Number(q.id)])
      }
    }
    persist()
  }

  static getInstance(): SyncEngine {
    if (!SyncEngine.instance) SyncEngine.instance = new SyncEngine()
    return SyncEngine.instance
  }

  // ---- 注册与状态 ----------------------------------------------------------

  registerTable(def: SyncTableDef): void {
    this.registeredTables.set(def.table, def)
  }

  /** bootstrap 阶段显式注册 6 张可写表（构造函数已默认注册，重复调用幂等） */
  registerDefaultTables(): void {
    for (const [table, api] of Object.entries(API_BY_TABLE)) {
      this.registerTable({ table, api })
    }
  }

  registerRemapHandlers(handlers: RemapHandlers): void {
    this.remapHandlers = handlers
  }

  /** 由 sync.ts 的 setSyncOnAuthFail 转发注入，避免引擎反向依赖 store 模块 */
  setAuthFailHandler(fn: (() => void) | null): void {
    this.authFailHandler = fn
  }

  reset(): void {
    this.queue = []
    this.lastSyncAt = null
    this.syncing = false
    this.pushErrors = []
    resetLocalDb()
    this.emitStateChanged()
  }

  getState(): SyncStateSnapshot {
    return {
      queue: [...this.queue],
      syncing: this.syncing,
      lastSyncAt: this.lastSyncAt,
      pushErrors: [...this.pushErrors],
    }
  }

  getConflicts(): SyncQueueItem[] {
    return this.queue.filter((q) => q.synced === 0 && q.conflict === 1)
  }

  getPushErrors(): PushError[] {
    return [...this.pushErrors]
  }

  clearPushErrors(): void {
    this.pushErrors = []
    this.emitStateChanged()
  }

  /** 从 DB 装载队列与 last_sync_at；initLocalDb() 之后调用 */
  init(): void {
    const res = query("SELECT value FROM sync_meta WHERE key = 'last_sync_at'")
    const value = (res as any)?.[0]?.values?.[0]?.[0]
    if (typeof value === 'string') this.lastSyncAt = value
    this.loadQueue()
    this.emitStateChanged()
  }

  // ---- 队列内部操作 --------------------------------------------------------

  private loadQueue(): void {
    const res = query(
      'SELECT id, table_name, record_id, operation, payload, local_version, synced, conflict, ledger_id, remote_payload FROM sync_queue WHERE synced = 0 ORDER BY id ASC'
    )
    this.queue = rowsFrom(res).map((r) => ({
      id: Number(r.id),
      table_name: String(r.table_name),
      record_id: Number(r.record_id),
      operation: r.operation as SyncQueueItem['operation'],
      payload: String(r.payload),
      local_version: String(r.local_version),
      synced: Number(r.synced),
      conflict: Number(r.conflict),
      ledger_id: Number(r.ledger_id || 0),
      remote_payload: parseRemotePayload(r.remote_payload),
    }))
    this.emitStateChanged()
  }

  private emitStateChanged(): void {
    if (typeof window === 'undefined') return
    window.dispatchEvent(new CustomEvent(SYNC_STATE_EVENT))
  }

  private markSynced(queueId: number): void {
    run('UPDATE sync_queue SET synced = 1 WHERE id = ?', [queueId])
    persist()
    this.loadQueue()
  }

  private markConflict(queueId: number, remotePayload?: Record<string, unknown>): void {
    const remote = remotePayload === undefined ? null : JSON.stringify(remotePayload)
    run('UPDATE sync_queue SET conflict = 1, remote_payload = ? WHERE id = ?', [remote, queueId])
    persist()
    this.loadQueue()
  }

  private setLastSync(at: string): void {
    this.lastSyncAt = at
    run("INSERT OR REPLACE INTO sync_meta (key, value) VALUES ('last_sync_at', ?)", [at])
    persist()
    this.emitStateChanged()
  }

  // ---- 公开 API ------------------------------------------------------------

  enqueue(
    tableName: string,
    recordId: number,
    operation: SyncOperation,
    payload: Record<string, unknown>,
    ledgerId?: number
  ): void {
    if (!this.registeredTables.has(tableName)) {
      throw new Error(`[SyncEngine] 未注册的离线同步表: ${tableName}`)
    }
    const queuePayload: Record<string, unknown> = { ...payload }
    const activeLedger = Number(
      ledgerId ??
      (payload as any)?.ledger_id ??
      localStorage.getItem('minefolio_active_ledger_id') ??
      0
    )
    if (queuePayload.ledger_id === undefined && activeLedger > 0) {
      queuePayload.ledger_id = activeLedger
    }
    try {
      this.writeLocalRow(tableName, operation, recordId, queuePayload)
    } catch (err) {
      // 本地镜像写入失败（如 payload 缺少必填列）不阻断入队：
      // 变更仍可推送到服务端，下次 pull 会补齐本地缓存。
      console.warn('[SyncEngine] 本地镜像写入失败，继续入队:', err)
    }

    run(
      'INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version, ledger_id, remote_payload) VALUES (?, ?, ?, ?, ?, ?, NULL)',
      [tableName, recordId, operation, JSON.stringify(queuePayload), new Date().toISOString(), activeLedger]
    )
    persist()
    this.loadQueue()
  }

  /** 本地 sqlite upsert（与既有 offline-http.writeLocal 行为一致） */
  private writeLocalRow(table: string, operation: SyncOperation, recordId: number, payload: Record<string, unknown>): void {
    if (operation === 'delete') {
      run(`UPDATE ${table} SET __deleted = 1 WHERE id = ?`, [recordId])
      persist()
      return
    }
    const allowed = TABLE_COLUMNS[table]
    const cols = Object.keys(payload).filter(
      (k) => k !== '__deleted' && k !== 'id' && (!allowed || allowed.includes(k)) && payload[k] !== undefined
    )
    if (cols.length === 0) {
      persist()
      return
    }
    const toSqlVal = (v: unknown): SqlValue => (typeof v === 'boolean' ? (v ? 1 : 0) : (v as SqlValue))
    const placeholders = cols.map(() => '?').join(', ')
    const updates = cols.map((c) => `${c} = ?`).join(', ')
    run(
      `INSERT INTO ${table} (${cols.join(', ')}, id, __deleted) VALUES (${placeholders}, ?, 0)
       ON CONFLICT(id) DO UPDATE SET ${updates}, __deleted = 0`,
      [...cols.map((c) => toSqlVal(payload[c])), recordId, ...cols.map((c) => toSqlVal(payload[c]))]
    )
    persist()
  }

  async pushLocal(): Promise<Map<string, Set<number>>> {
    const pushedInCycle = new Map<string, Set<number>>()
    this.loadQueue()
    for (const item of this.queue) {
      if (item.conflict === 1) continue
      const def = this.registeredTables.get(item.table_name)
      if (!def) continue
      // 逐条重读 DB：record_id / payload 可能被本轮更早的 create remap 改写
      const fresh = rowsFrom(query(
        'SELECT record_id, operation, payload, ledger_id, conflict FROM sync_queue WHERE id = ?',
        [item.id]
      ))[0]
      if (!fresh || Number(fresh.conflict) === 1) continue
      const recordId = Number(fresh.record_id)
      const operation = fresh.operation as SyncOperation
      const itemLedgerId = Number(fresh.ledger_id || item.ledger_id || 0)
      const payload = parsePayload(fresh.payload)
      // 账本为入队时快照，不读取当前活跃账本
      const config = itemLedgerId > 0 ? { headers: { 'X-Ledger-Id': String(itemLedgerId) } } : undefined
      try {
        let effectiveId = recordId
        if (operation === 'create') {
          const res: any = config ? await def.api.create(payload, config) : await def.api.create(payload)
          const newId = res?.id ?? (typeof res === 'number' ? res : null)
          if (newId) effectiveId = Number(newId)
          if (newId && newId !== recordId) {
            run(`UPDATE ${item.table_name} SET id = ? WHERE id = ?`, [newId, recordId])
            run(
              'UPDATE sync_queue SET record_id = ? WHERE table_name = ? AND record_id = ? AND id > ?',
              [newId, item.table_name, recordId, item.id]
            )
            if (item.table_name === 'assets') {
              this.remapHandlers?.onAssetRemap(recordId, newId, item.id)
            } else if (item.table_name === 'accounts') {
              this.remapHandlers?.onAccountRemap(recordId, newId, item.id)
            } else if (item.table_name === 'categories') {
              this.remapHandlers?.onCategoryRemap?.(recordId, newId, item.id)
            } else if (item.table_name === 'tags') {
              this.remapHandlers?.onTagRemap?.(recordId, newId, item.id)
            } else {
              persist()
            }
          }
        } else if (operation === 'update') {
          if (config) await def.api.update(recordId, payload, config)
          else await def.api.update(recordId, payload)
        } else if (operation === 'delete') {
          if (config) await def.api.delete(recordId, config)
          else await def.api.delete(recordId)
        }
        this.markSynced(item.id)
        if (operation !== 'delete' && effectiveId > 0) {
          let set = pushedInCycle.get(item.table_name)
          if (!set) {
            set = new Set<number>()
            pushedInCycle.set(item.table_name, set)
          }
          set.add(effectiveId)
        }
      } catch (e: any) {
        const code = Number(e?.response?.data?.code || 0)
        const status = Number(e?.response?.status || 0)
        if (code === AUTH_FAIL_CODE) {
          this.authFailHandler?.()
          throw e
        }
        if (
          code === CONFLICT_CODE ||
          (status >= 400 && status < 500 && status !== 401 && status !== 408 && status !== 400 && status !== 422 && status !== 429)
        ) {
          // 业务冲突：1004 响应不含记录数据，remote_payload 留空
          this.markConflict(item.id)
        } else if (VALIDATION_CODES.has(code) || status === 400 || status === 422) {
          // 校验/权限类永久失败：放弃队列项，记录错误供设置页提示；create 失败一并清理本地占位行
          const message = String(e?.response?.data?.message || e?.message || '离线数据同步失败')
          if (operation === 'create') {
            run(`DELETE FROM ${item.table_name} WHERE id = ?`, [recordId])
          }
          run('DELETE FROM sync_queue WHERE id = ?', [item.id])
          this.pushErrors.push({ queueId: item.id, table: item.table_name, message })
          persist()
          this.loadQueue()
        }
        // 429 / 5xx / 超时：保持 synced=0，下次 syncNow 重试
      }
    }
    return pushedInCycle
  }

  async pullRemote(pushedInCycle?: Map<string, Set<number>>): Promise<void> {
    const tables = Object.keys(API_BY_TABLE)
    for (const table of tables) {
      const api = this.registeredTables.get(table)?.api ?? API_BY_TABLE[table]
      if (!api?.list) continue
      try {
        let page = 1
        const pageSize = 500
        const seenIds = new Set<number>()
        let completedAllPages = false
        while (true) {
          const params: Record<string, any> = { page, page_size: pageSize }
          if (table === 'accounts') {
            params.include_archived = 1
          }
          const res: any = await api.list(params)
          const rawRows = (res?.list ?? (Array.isArray(res) ? res : [])) as any[]
          const remoteRows = table === 'categories' ? flattenCategoryTree(rawRows) : rawRows
          for (const remote of remoteRows) {
            if (remote?.id !== undefined && remote?.id !== null) seenIds.add(Number(remote.id))
            const localRow = rowsFrom(query(`SELECT * FROM ${table} WHERE id = ?`, [remote.id]))[0]
            const localUpdated = parseSyncTimestamp(localRow?.updated_at)
            const remoteUpdated = parseSyncTimestamp(remote.updated_at)
            const pending = rowsFrom(query(
              'SELECT id FROM sync_queue WHERE table_name = ? AND record_id = ? AND synced = 0',
              [table, remote.id]
            ))
            const pendingItem = pending[0]
            if (pendingItem) {
              // 本地有未推送变更 → 冲突，保留本地行与 payload，填充远端版本
              this.markConflict(Number(pendingItem.id), remote as Record<string, unknown>)
              continue
            }
            if (!localRow || remoteUpdated >= localUpdated) {
              this.upsertLocal(table, remote)
            }
          }
          if (
            rawRows.length === 0 ||
            Array.isArray(res) ||
            rawRows.length < pageSize ||
            (res?.total !== undefined && page * pageSize >= res.total)
          ) {
            completedAllPages = true
            break
          }
          page++
        }
        if (completedAllPages) {
          const pendingIds = new Set(rowsFrom(
            query('SELECT DISTINCT record_id FROM sync_queue WHERE table_name = ? AND synced = 0', [table])
          ).map((r) => Number(r.record_id)))
          const pushedIds = pushedInCycle?.get(table) ?? new Set<number>()
          const hasLedgerId = ['categories', 'assets', 'accounts', 'account_permissions', 'transactions', 'daily_expenses'].includes(table)
          const activeLedgerId = Number(localStorage.getItem('minefolio_active_ledger_id') || 0)
          let localRows: any[] = []
          if (hasLedgerId) {
            if (activeLedgerId > 0) {
              localRows = rowsFrom(query(
                `SELECT id FROM ${table} WHERE __deleted = 0 AND (ledger_id = ? OR ledger_id IS NULL)`,
                [activeLedgerId]
              ))
            } else {
              localRows = rowsFrom(query(
                `SELECT id FROM ${table} WHERE __deleted = 0 AND (ledger_id = 0 OR ledger_id IS NULL)`
              ))
            }
          } else {
            localRows = rowsFrom(query(`SELECT id FROM ${table} WHERE __deleted = 0`))
          }
          for (const lr of localRows) {
            const lid = Number(lr.id)
            if (lid > 0 && !seenIds.has(lid) && !pendingIds.has(lid) && !pushedIds.has(lid)) {
              run(`DELETE FROM ${table} WHERE id = ?`, [lid])
            }
          }
        }
      } catch (err) {
        console.error(`[sync] Failed to pull remote data for table ${table}:`, err)
      }
    }
    persist()
    this.emitStateChanged()
  }

  private upsertLocal(table: string, record: Record<string, unknown>): void {
    const allowed = TABLE_COLUMNS[table]
    const cols = Object.keys(record).filter(
      (k) => k !== '__deleted' && (!allowed || allowed.includes(k)) && record[k] !== undefined
    )
    if (cols.length === 0) return
    const toSqlVal = (v: unknown): SqlValue => (typeof v === 'boolean' ? (v ? 1 : 0) : (v as SqlValue))
    const placeholders = cols.map(() => '?').join(', ')
    const updates = cols.map((c) => `${c} = ?`).join(', ')
    run(
      `INSERT INTO ${table} (${cols.join(', ')}, __deleted) VALUES (${placeholders}, 0)
       ON CONFLICT(id) DO UPDATE SET ${updates}, __deleted = 0`,
      [...cols.map((c) => toSqlVal(record[c])), ...cols.map((c) => toSqlVal(record[c]))]
    )
  }

  async syncNow(): Promise<void> {
    if (this.syncing) return
    this.syncing = true
    this.emitStateChanged()
    try {
      const pushedInCycle = await this.pushLocal()
      await this.pullRemote(pushedInCycle)
      this.setLastSync(new Date().toISOString())
    } catch (e: any) {
      // 网络/超时静默；鉴权失效由 pushLocal 调用 handler 后抛出
      const code = Number(e?.response?.data?.code || 0)
      if (code === AUTH_FAIL_CODE) this.authFailHandler?.()
    } finally {
      this.syncing = false
      this.emitStateChanged()
    }
  }

  async resolveConflict(queueId: number, action: 'retry' | 'discard'): Promise<void> {
    if (action === 'retry') {
      run('UPDATE sync_queue SET conflict = 0, remote_payload = NULL WHERE id = ?', [queueId])
      persist()
      this.loadQueue()
      await this.syncNow()
    } else {
      const row = rowsFrom(query(
        'SELECT table_name, record_id, operation, remote_payload FROM sync_queue WHERE id = ?',
        [queueId]
      ))[0]
      if (row) {
        const tableName = String(row.table_name)
        const recordId = Number(row.record_id)
        const operation = String(row.operation) as SyncOperation
        const remotePayload = parseRemotePayload(row.remote_payload)
        if (this.registeredTables.has(tableName)) {
          if (operation === 'create') {
            run(`DELETE FROM ${tableName} WHERE id = ?`, [recordId])
          } else if (remotePayload) {
            this.upsertLocal(tableName, { ...remotePayload, id: remotePayload.id ?? recordId })
          } else if (operation === 'delete') {
            run(`UPDATE ${tableName} SET __deleted = 0 WHERE id = ?`, [recordId])
          }
        }
      }
      run('DELETE FROM sync_queue WHERE id = ?', [queueId])
      persist()
      this.loadQueue()
    }
  }
}
