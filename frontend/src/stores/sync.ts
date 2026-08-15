// frontend/src/stores/sync.ts
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { getDb, run, query, rowsFrom, persist } from '@/db/local'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import type { SyncQueueItem, SqlValue } from '@/types/mobile'

const API_BY_TABLE: Record<string, any> = {
  daily_expenses: dailyExpensesApi,
  transactions: transactionsApi,
  assets: assetsApi,
}

// 鉴权失败(1001)处理钩子：由移动端 main/router 注入，把桌面式 window.location 重定向换成路由导航
let onAuthFail: (() => void) | null = null
export function setSyncOnAuthFail(fn: (() => void) | null): void {
  onAuthFail = fn
}

function isAuthError(e: any): boolean {
  return e?.response?.data?.code === 1001
}

export const useSyncStore = defineStore('sync', () => {
  const queue = ref<SyncQueueItem[]>([])
  const syncing = ref(false)
  const lastSyncAt = ref<string | null>(null)
  const pendingCount = computed(() => queue.value.filter((q) => q.synced === 0).length)

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

  function setLastSync(at: string): void {
    lastSyncAt.value = at
    run("INSERT OR REPLACE INTO sync_meta (key, value) VALUES ('last_sync_at', ?)", [at])
    persist()
  }

  // 入队：本地已写，记录待推送
  function enqueue(tableName: string, recordId: number, operation: SyncQueueItem['operation'], payload: Record<string, unknown>): void {
    run(
      'INSERT INTO sync_queue (table_name, record_id, operation, payload, local_version) VALUES (?, ?, ?, ?, ?)',
      [tableName, recordId, operation, JSON.stringify(payload), new Date().toISOString()]
    )
    persist()
    loadQueue()
  }

  function markSynced(queueId: number): void {
    run('UPDATE sync_queue SET synced = 1 WHERE id = ?', [queueId])
    persist()
    loadQueue()
  }

  function markConflict(queueId: number): void {
    run('UPDATE sync_queue SET conflict = 1 WHERE id = ?', [queueId])
    persist()
    loadQueue()
  }

  // 推本地队列到服务端
  async function pushLocal(): Promise<void> {
    loadQueue()
    for (const item of queue.value) {
      const api = API_BY_TABLE[item.table_name]
      if (!api) continue
      const payload = JSON.parse(item.payload)
      try {
        if (item.operation === 'create') await api.create(payload)
        else if (item.operation === 'update') await api.update(item.record_id, payload)
        else if (item.operation === 'delete') await api.delete(item.record_id)
        markSynced(item.id)
      } catch (e: any) {
        // 鉴权失败：注销 + 触发移动端路由（🔴 不要标冲突污染队列）
        if (isAuthError(e)) {
          onAuthFail?.()
          return
        }
        // 服务端业务错误（如已不存在）→ 标记冲突，停止该条推送但保留队列
        if (e?.response?.data?.code && e.response.data.code !== 0) markConflict(item.id)
      }
    }
  }

  // 拉远程到本地（服务端胜出：updated_at 较新则覆盖）
  async function pullRemote(): Promise<void> {
    const tables = ['daily_expenses', 'transactions', 'assets']
    for (const table of tables) {
      const api = API_BY_TABLE[table]
      if (!api?.list) continue
      const res: any = await api.list({ page_size: 500 })
      const remoteRows = (res.list ?? []) as any[]
      if (remoteRows.length === 0) continue
      for (const remote of remoteRows) {
        const local = query(`SELECT * FROM ${table} WHERE id = ?`, [remote.id])
        const localRow = rowsFrom(local)[0]
        const localUpdated = localRow?.updated_at ? new Date(localRow.updated_at as string).getTime() : 0
        const remoteUpdated = remote.updated_at ? new Date(remote.updated_at).getTime() : 0
        if (!localRow || remoteUpdated >= localUpdated) {
          upsertLocal(table, remote)
        }
      }
    }
    persist()
  }

  function upsertLocal(table: string, record: Record<string, unknown>): void {
    const cols = Object.keys(record).filter((k) => k !== '__deleted')
    const placeholders = cols.map(() => '?').join(', ')
    const updates = cols.map((c) => `${c} = ?`).join(', ')
    run(
      `INSERT INTO ${table} (${cols.join(', ')}, __deleted) VALUES (${placeholders}, 0)
       ON CONFLICT(id) DO UPDATE SET ${updates}, __deleted = 0`,
      [...cols.map((c) => record[c] as SqlValue), ...cols.map((c) => record[c] as SqlValue)]
    )
  }

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

  function init(): void {
    getDb() // 确保已 init
    const res = query("SELECT value FROM sync_meta WHERE key = 'last_sync_at'")
    const value = res[0]?.values?.[0]?.[0]
    if (typeof value === 'string') lastSyncAt.value = value
    loadQueue()
  }

  return { queue, syncing, lastSyncAt, pendingCount, enqueue, markSynced, markConflict, pushLocal, pullRemote, syncNow, init }
})
