/**
 * @file 移动端离线数据双向同步状态管理 Store
 * @description 管理离线 SQLite 数据库队列写入、网络恢复时本地变更推送到服务端 (Push)、服务端最新变更拉取覆盖本地 (Pull) 与冲突标记
 */

// frontend/src/stores/sync.ts
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { getDb, run, query, rowsFrom, persist } from '@/db/local'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import type { SyncQueueItem, SqlValue } from '@/types/mobile'

/** 表名到对应 API 模块的映射字典 */
const API_BY_TABLE: Record<string, any> = {
  daily_expenses: dailyExpensesApi,
  transactions: transactionsApi,
  assets: assetsApi,
}

/** 允许本地同步写入的白名单列集合 */
const TABLE_COLUMNS: Record<string, Set<string>> = {
  categories: new Set(['id', 'user_id', 'name', 'parent_id', 'type', 'asset_type', 'currency', 'icon', 'sort_order', 'created_at', 'updated_at']),
  assets: new Set(['id', 'user_id', 'category_id', 'name', 'account_no', 'current_value', 'quantity', 'cost_basis', 'net_value', 'currency', 'note', 'created_at', 'updated_at']),
  transactions: new Set(['id', 'user_id', 'asset_id', 'linked_asset_id', 'category_id', 'source_type', 'transaction_type', 'direction', 'linked_direction', 'amount', 'price_per_unit', 'quantity', 'fee', 'currency', 'transaction_date', 'note', 'created_at', 'updated_at']),
  tags: new Set(['id', 'user_id', 'name', 'color', 'created_at', 'updated_at']),
  daily_expenses: new Set(['id', 'user_id', 'category_id', 'asset_id', 'expense_type', 'amount', 'currency', 'expense_date', 'note', 'created_at', 'updated_at']),
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
      'SELECT id, table_name, record_id, operation, payload, local_version, synced, conflict FROM sync_queue WHERE synced = 0 ORDER BY id ASC'
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
   */
  function enqueue(tableName: string, recordId: number, operation: SyncQueueItem['operation'], payload: Record<string, unknown>): void {
    run(
      'INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version) VALUES (?, ?, ?, ?, ?)',
      [tableName, recordId, operation, JSON.stringify(payload), new Date().toISOString()]
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
   * 将本地离线队列中的数据依次推送到服务端 API (Push)
   */
  async function pushLocal(): Promise<void> {
    loadQueue()
    for (const item of queue.value) {
      const api = API_BY_TABLE[item.table_name]
      if (!api) continue
      // 实时读取队列行：record_id 可能已被本轮更早的 create 重映射（资产建占位 ID
      // → 服务端 ID），payload 也可能被 remapAssetRefs 改写，必须用最新版本推送。
      const fresh = rowsFrom(query(
        'SELECT record_id, operation, payload FROM sync_queue WHERE id = ?',
        [item.id]
      ))[0]
      if (!fresh) continue
      const recordId = Number(fresh.record_id)
      const operation = fresh.operation as SyncQueueItem['operation']
      let payload: Record<string, unknown>
      try {
        payload = JSON.parse(String(fresh.payload))
      } catch {
        continue
      }
      try {
        if (operation === 'create') {
          const res: any = await api.create(payload)
          const newId = res?.id ?? (typeof res === 'number' ? res : null)
          if (newId && newId !== recordId) {
            run(`UPDATE ${item.table_name} SET id = ? WHERE id = ?`, [newId, recordId])
            run(
              'UPDATE sync_queue SET record_id = ? WHERE table_name = ? AND record_id = ? AND id > ?',
              [newId, item.table_name, recordId, item.id]
            )
            if (item.table_name === 'assets') {
              // 资产获得真实 ID 后，同步重映射本地依赖行与后续待推送 payload
              remapAssetRefs(recordId, newId, item.id)
            } else {
              persist()
            }
          }
        } else if (operation === 'update') {
          await api.update(recordId, payload)
        } else if (operation === 'delete') {
          await api.delete(recordId)
        }
        markSynced(item.id)
      } catch (e: any) {
        // 鉴权失败：注销 + 触发移动端路由（不要标冲突污染队列）
        if (isAuthError(e)) {
          onAuthFail?.()
          return
        }
        // 服务端业务错误（如已不存在）→ 标记冲突，停止该条推送但保留队列
        if (e?.response?.data?.code && e.response.data.code !== 0) markConflict(item.id)
      }
    }
  }

  /**
   * 从服务端拉取最新数据覆盖本地 SQLite (Pull，服务端胜出策略：updated_at 较新则覆盖)
   */
  async function pullRemote(): Promise<void> {
    const tables = ['daily_expenses', 'transactions', 'assets']
    for (const table of tables) {
      const api = API_BY_TABLE[table]
      if (!api?.list) continue
      try {
        let page = 1
        const pageSize = 500
        while (true) {
          const res: any = await api.list({ page, page_size: pageSize })
          const remoteRows = (res?.list ?? (Array.isArray(res) ? res : [])) as any[]
          if (remoteRows.length === 0) break
          for (const remote of remoteRows) {
            const local = query(`SELECT * FROM ${table} WHERE id = ?`, [remote.id])
            const localRow = rowsFrom(local)[0]
            const localUpdated = localRow?.updated_at ? new Date(localRow.updated_at as string).getTime() : 0
            const remoteUpdated = remote.updated_at ? new Date(remote.updated_at).getTime() : 0
            if (!localRow || remoteUpdated >= localUpdated) {
              upsertLocal(table, remote)
            }
          }
          if (remoteRows.length < pageSize || (res?.total !== undefined && page * pageSize >= res.total)) {
            break
          }
          page++
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
    const cols = Object.keys(record).filter((k) => k !== '__deleted' && (!allowed || allowed.has(k)) && record[k] !== undefined)
    if (cols.length === 0) return
    const placeholders = cols.map(() => '?').join(', ')
    const updates = cols.map((c) => `${c} = ?`).join(', ')
    run(
      `INSERT INTO ${table} (${cols.join(', ')}, __deleted) VALUES (${placeholders}, 0)
       ON CONFLICT(id) DO UPDATE SET ${updates}, __deleted = 0`,
      [...cols.map((c) => record[c] as SqlValue), ...cols.map((c) => record[c] as SqlValue)]
    )
  }

  /**
   * 立即执行一次完整的双向增量数据同步 (Push 本地队列 → Pull 远端最新)
   */
  async function syncNow(): Promise<void> {
    if (syncing.value) return
    syncing.value = true
    try {
      await pushLocal()
      await pullRemote()
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
  }
})

