/**
 * @file 移动端离线数据双向同步状态管理 Store
 * @description 管理离线 SQLite 数据库队列写入、网络恢复时本地变更推送到服务端 (Push)、服务端最新变更拉取覆盖本地 (Pull) 与冲突标记
 */

// frontend/src/stores/sync.ts
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { getDb, run, query, rowsFrom, persist, resetLocalDb } from '@/db/local'
import { TABLE_COLUMNS } from '@/db/schema'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import { accountsApi } from '@/api/accounts'
import { categoriesApi } from '@/api/categories'
import { tagsApi } from '@/api/tags'
import type { SyncQueueItem, SqlValue } from '@/types/mobile'

/** 表名到对应 API 模块的映射字典 */
const API_BY_TABLE: Record<string, any> = {
  daily_expenses: dailyExpensesApi,
  transactions: transactionsApi,
  assets: assetsApi,
  accounts: accountsApi,
  categories: categoriesApi,
  tags: tagsApi,
}

/** 鉴权失败(1001)处理钩子：由移动端 main/router 注入，把桌面式 window.location 重定向换成路由导航 */
let onAuthFail: (() => void) | null = null

/**
 * 注入移动端鉴权失效回调处理函数
 * @param fn 回调函数
 */
export function setSyncOnAuthFail(fn: (() => void) | null): void {
  onAuthFail = fn
}

/**
 * 判断异常是否为 1001 鉴权未授权错误
 * @param e 异常对象
 * @returns 是否鉴权失效
 */
function isAuthError(e: any): boolean {
  return e?.response?.data?.code === 1001
}

/**
 * 离线同步 Pinia Store
 */
export const useSyncStore = defineStore('sync', () => {
  /** 本地未完成同步的队列任务列表 */
  const queue = ref<SyncQueueItem[]>([])
  /** 是否正在执行双向同步流程 */
  const syncing = ref(false)
  /** 上次成功完成全量同步的 ISO 时间戳 */
  const lastSyncAt = ref<string | null>(null)
  /** 待推送同步的任务数量计算属性 */
  const pendingCount = computed(() => queue.value.filter((q) => q.synced === 0).length)

  /**
   * 从本地 SQLite 数据库中加载所有未同步 (synced = 0) 的队列项
   */
  function loadQueue(): void {
    const res = query(
      'SELECT id, table_name, record_id, operation, payload, local_version, synced, conflict, ledger_id FROM sync_queue WHERE synced = 0 ORDER BY id ASC'
    )
    queue.value = rowsFrom(res).map((r) => ({
      id: r.id as number,
      table_name: r.table_name as string,
      record_id: r.record_id as number,
      operation: r.operation as SyncQueueItem['operation'],
      payload: r.payload as string,
      local_version: r.local_version as string,
      synced: r.synced as number,
      conflict: r.conflict as number,
      ledger_id: Number(r.ledger_id || 0),
    }))
  }

  /**
   * 记录最后同步时间戳并持久化到本地元数据表
   * @param at ISO 8601 时间字符串
   */
  function setLastSync(at: string): void {
    lastSyncAt.value = at
    run("INSERT OR REPLACE INTO sync_meta (key, value) VALUES ('last_sync_at', ?)", [at])
    persist()
  }

  /**
   * 将本地写操作放入待推送同步队列
   * @param tableName 业务表名 (如 'daily_expenses', 'transactions', 'assets')
   * @param recordId 本地记录 ID
   * @param operation 操作类型 ('create' | 'update' | 'delete')
   * @param payload 记录数据载荷
   * @param ledgerId 可选指定账本 ID（不指定则优先使用 payload.ledger_id 或当前活跃账本）
   */
  function enqueue(
    tableName: string,
    recordId: number,
    operation: SyncQueueItem['operation'],
    payload: Record<string, unknown>,
    ledgerId?: number
  ): void {
    const activeLedger = Number(
      ledgerId ??
      (payload as any)?.ledger_id ??
      localStorage.getItem('minefolio_active_ledger_id') ??
      0
    )
    run(
      'INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version, ledger_id) VALUES (?, ?, ?, ?, ?, ?)',
      [tableName, recordId, operation, JSON.stringify(payload), new Date().toISOString(), activeLedger]
    )
    persist()
    loadQueue()
  }

  /**
   * 将指定队列项标记为已同步 (synced = 1)
   * @param queueId 队列项 ID
   */
  function markSynced(queueId: number): void {
    run('UPDATE sync_queue SET synced = 1 WHERE id = ?', [queueId])
    persist()
    loadQueue()
  }

  /**
   * 将指定队列项标记为冲突状态 (conflict = 1)
   * @param queueId 队列项 ID
   */
  function markConflict(queueId: number): void {
    run('UPDATE sync_queue SET conflict = 1 WHERE id = ?', [queueId])
    persist()
    loadQueue()
  }

  /**
   * 将离线创建的资产获得服务端真实 ID 后，把本地与其关联的行以及待推送队列中
   * 依赖记录的 payload (daily_expenses.asset_id / transactions.asset_id|linked_asset_id)
   * 一并重映射，避免依赖记录携带过期的本地占位 ID 推送到服务端。
   * @param oldId 本地占位 ID
   * @param newId 服务端分配的真实 ID
   * @param afterQueueId 只处理排在当前 create 之后的队列项
   */
  function remapAssetRefs(oldId: number, newId: number, afterQueueId: number): void {
    // 本地关联行
    run('UPDATE daily_expenses SET asset_id = ? WHERE asset_id = ?', [newId, oldId])
    run('UPDATE transactions SET asset_id = ? WHERE asset_id = ?', [newId, oldId])
    run('UPDATE transactions SET linked_asset_id = ? WHERE linked_asset_id = ?', [newId, oldId])
    // 待推送队列中依赖记录的 payload（含 create/update/delete）
    const queued = rowsFrom(query(
      "SELECT id, table_name, payload FROM sync_queue WHERE synced = 0 AND id > ? AND table_name IN ('daily_expenses', 'transactions')",
      [afterQueueId]
    ))
    for (const q of queued) {
      let payload: Record<string, unknown>
      try {
        payload = JSON.parse(String(q.payload))
      } catch {
        continue
      }
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
      if (changed) {
        run('UPDATE sync_queue SET payload = ? WHERE id = ?', [JSON.stringify(payload), Number(q.id)])
      }
    }
    persist()
  }

  /**
   * 将离线创建的账户获得服务端真实 ID 后，把本地 assets.account_id 以及
   * 后续待推送队列中 assets 记录的 account_id 字段一并重映射。
   * @param oldId 本地占位 ID
   * @param newId 服务端分配的真实 ID
   * @param afterQueueId 只处理排在当前 create 之后的队列项
   */
  function remapAccountRefs(oldId: number, newId: number, afterQueueId: number): void {
    run('UPDATE assets SET account_id = ? WHERE account_id = ?', [newId, oldId])
    const queued = rowsFrom(query(
      "SELECT id, payload FROM sync_queue WHERE synced = 0 AND id > ? AND table_name = 'assets'",
      [afterQueueId]
    ))
    for (const q of queued) {
      let payload: Record<string, unknown>
      try {
        payload = JSON.parse(String(q.payload))
      } catch {
        continue
      }
      if (payload.account_id === oldId) {
        payload.account_id = newId
        run('UPDATE sync_queue SET payload = ? WHERE id = ?', [JSON.stringify(payload), Number(q.id)])
      }
    }
    persist()
  }

  /**
   * 将本地离线队列中的数据依次推送到服务端 API (Push)
   * @returns 本轮成功推送创建/更新的服务端 ID 映射（供 pullRemote 协调删除时保留）
   */
  async function pushLocal(): Promise<Map<string, Set<number>>> {
    const pushedInCycle = new Map<string, Set<number>>()
    loadQueue()
    for (const item of queue.value) {
      const api = API_BY_TABLE[item.table_name]
      if (!api) continue
      // 实时读取队列行：record_id 可能已被本轮更早的 create 重映射（资产建占位 ID
      // → 服务端 ID），payload 也可能被 remapAssetRefs 改写，必须用最新版本推送。
      const fresh = rowsFrom(query(
        'SELECT record_id, operation, payload, ledger_id FROM sync_queue WHERE id = ?',
        [item.id]
      ))[0]
      if (!fresh) continue
      const recordId = Number(fresh.record_id)
      const operation = fresh.operation as SyncQueueItem['operation']
      const itemLedgerId = Number(fresh.ledger_id || item.ledger_id || 0)
      let payload: Record<string, unknown>
      try {
        payload = JSON.parse(String(fresh.payload))
      } catch {
        continue
      }
      const config = itemLedgerId > 0 ? { headers: { 'X-Ledger-Id': String(itemLedgerId) } } : undefined
      try {
        let effectiveId = recordId
        if (operation === 'create') {
          const res: any = config ? await api.create(payload, config) : await api.create(payload)
          const newId = res?.id ?? (typeof res === 'number' ? res : null)
          if (newId) {
            effectiveId = Number(newId)
          }
          if (newId && newId !== recordId) {
            run(`UPDATE ${item.table_name} SET id = ? WHERE id = ?`, [newId, recordId])
            run(
              'UPDATE sync_queue SET record_id = ? WHERE table_name = ? AND record_id = ? AND id > ?',
              [newId, item.table_name, recordId, item.id]
            )
            if (item.table_name === 'assets') {
              // 资产获得真实 ID 后，同步重映射本地依赖行与后续待推送 payload
              remapAssetRefs(recordId, newId, item.id)
            } else if (item.table_name === 'accounts') {
              remapAccountRefs(recordId, newId, item.id)
            } else {
              persist()
            }
          }
        } else if (operation === 'update') {
          if (config) {
            await api.update(recordId, payload, config)
          } else {
            await api.update(recordId, payload)
          }
        } else if (operation === 'delete') {
          if (config) {
            await api.delete(recordId, config)
          } else {
            await api.delete(recordId)
          }
        }
        markSynced(item.id)
        if (operation !== 'delete' && effectiveId > 0) {
          let set = pushedInCycle.get(item.table_name)
          if (!set) {
            set = new Set<number>()
            pushedInCycle.set(item.table_name, set)
          }
          set.add(effectiveId)
        }
      } catch (e: any) {
        // 鉴权失败：注销 + 触发移动端路由（不要标冲突污染队列）
        if (isAuthError(e)) {
          onAuthFail?.()
          throw e
        }
        // 服务端业务错误（如已不存在）→ 标记冲突，停止该条推送但保留队列
        if (e?.response?.data?.code && e.response.data.code !== 0) markConflict(item.id)
      }
    }
    return pushedInCycle
  }

  /**
   * 从服务端拉取最新数据覆盖本地 SQLite (Pull，服务端胜出策略：updated_at 较新则覆盖，并在全量分页拉取完整时清理远端已删除行)
   */
  async function pullRemote(pushedInCycle?: Map<string, Set<number>>): Promise<void> {
    const tables = ['accounts', 'categories', 'tags', 'assets', 'transactions', 'daily_expenses']
    for (const table of tables) {
      const api = API_BY_TABLE[table]
      if (!api?.list) continue
      try {
        let page = 1
        const pageSize = 500
        const seenIds = new Set<number>()
        let completedAllPages = false
        while (true) {
          const res: any = await api.list({ page, page_size: pageSize })
          const remoteRows = (res?.list ?? (Array.isArray(res) ? res : [])) as any[]
          for (const remote of remoteRows) {
            if (remote?.id !== undefined && remote?.id !== null) {
              seenIds.add(Number(remote.id))
            }
            const local = query(`SELECT * FROM ${table} WHERE id = ?`, [remote.id])
            const localRow = rowsFrom(local)[0]
            const localUpdated = localRow?.updated_at ? new Date(localRow.updated_at as string).getTime() : 0
            const remoteUpdated = remote.updated_at ? new Date(remote.updated_at).getTime() : 0
            if (!localRow || remoteUpdated >= localUpdated) {
              upsertLocal(table, remote)
            }
          }
          if (
            remoteRows.length === 0 ||
            Array.isArray(res) ||
            remoteRows.length < pageSize ||
            (res?.total !== undefined && page * pageSize >= res.total)
          ) {
            completedAllPages = true
            break
          }
          page++
        }
        if (completedAllPages) {
          const pendingRows = rowsFrom(
            query('SELECT DISTINCT record_id FROM sync_queue WHERE table_name = ? AND synced = 0', [table])
          )
          const pendingIds = new Set(pendingRows.map((r) => Number(r.record_id)))
          const pushedIds = pushedInCycle?.get(table) ?? new Set<number>()
          const localRows = rowsFrom(query(`SELECT id FROM ${table} WHERE __deleted = 0`))
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
  }

  /**
   * 将远端单条记录 Upsert (插入或更新) 到本地 SQLite 数据表
   * @param table 表名
   * @param record 记录实体
   */
  function upsertLocal(table: string, record: Record<string, unknown>): void {
    const allowed = TABLE_COLUMNS[table]
    const cols = Object.keys(record).filter((k) => k !== '__deleted' && (!allowed || allowed.includes(k)) && record[k] !== undefined)
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

  /**
   * 立即执行一次完整的双向增量数据同步 (Push 本地队列 → Pull 远端最新)
   */
  async function syncNow(): Promise<void> {
    if (syncing.value) return
    syncing.value = true
    try {
      const pushedInCycle = await pushLocal()
      await pullRemote(pushedInCycle)
      setLastSync(new Date().toISOString())
    } catch (e: any) {
      // 网络/超时等：静默，保持队列待下次同步
      if (isAuthError(e)) onAuthFail?.()
    } finally {
      syncing.value = false
    }
  }

  /**
   * 初始化同步 Store (打开本地数据库并加载待办队列与同步元数据)
   */
  function init(): void {
    getDb() // 确保已 init
    const res = query("SELECT value FROM sync_meta WHERE key = 'last_sync_at'")
    const value = res[0]?.values?.[0]?.[0]
    if (typeof value === 'string') lastSyncAt.value = value
    loadQueue()
  }

  /**
   * 重置同步 Store (登出时调用)：清空内存队列与同步位，避免上一账号的离线
   * 写操作被下一账号 syncNow() 顶着新 token 推出去；同时丢弃本地库，
   * 下次登录 init() 会重建，天然按账号隔离。
   */
  function reset(): void {
    queue.value = []
    lastSyncAt.value = null
    syncing.value = false
    resetLocalDb()
  }

  return {
    queue,
    syncing,
    lastSyncAt,
    pendingCount,
    enqueue,
    markSynced,
    markConflict,
    pushLocal,
    pullRemote,
    syncNow,
    init,
    reset,
  }
})

