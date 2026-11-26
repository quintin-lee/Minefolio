/**
 * @file 移动端离线数据双向同步状态管理 Store（委托包装）
 * @description 所有实际同步逻辑由 SyncEngine 单例执行；本 Store 仅转发调用并维护 UI 消费的缓存快照。
 */

// frontend/src/stores/sync.ts
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { SyncEngine } from '@/sync/SyncEngine'
import type { PushError } from '@/sync/SyncEngine'
import type { SyncQueueItem } from '@/types/mobile'

export { parseSyncTimestamp } from '@/sync/SyncEngine'

/** 注入移动端鉴权失效回调处理函数（转发给 SyncEngine，保持对外 API 不变） */
export function setSyncOnAuthFail(fn: (() => void) | null): void {
  SyncEngine.getInstance().setAuthFailHandler(fn)
}

let subscribed = false

export const useSyncStore = defineStore('sync', () => {
  const engine = SyncEngine.getInstance()

  const queue = ref<SyncQueueItem[]>([])
  const syncing = ref(false)
  const lastSyncAt = ref<string | null>(null)
  const pushErrors = ref<PushError[]>([])
  const pendingCount = computed(() => queue.value.filter((q) => q.synced === 0).length)
  const conflicts = computed(() => queue.value.filter((q) => q.synced === 0 && q.conflict === 1))
  const activePendingCount = computed(() => queue.value.filter((q) => q.synced === 0 && q.conflict === 0).length)

  /** 从 SyncEngine 快照对齐 UI 缓存 */
  function syncFromEngine(): void {
    const state = engine.getState()
    queue.value = state.queue
    syncing.value = state.syncing
    lastSyncAt.value = state.lastSyncAt
    pushErrors.value = state.pushErrors
  }

  /** 订阅引擎状态事件（bootstrap 阶段初始化完成后调用一次） */
  function subscribeSyncEngine(): void {
    if (typeof window === 'undefined') return
    if (!subscribed) {
      subscribed = true
      window.addEventListener('minefolio:sync-state-changed', syncFromEngine)
    }
    syncFromEngine()
  }

  function init(): void {
    engine.init()
    syncFromEngine()
  }

  function enqueue(
    tableName: string,
    recordId: number,
    operation: SyncQueueItem['operation'],
    payload: Record<string, unknown>,
    ledgerId?: number
  ): void {
    engine.enqueue(tableName, recordId, operation, payload, ledgerId)
    syncFromEngine()
  }

  async function syncNow(): Promise<void> {
    await engine.syncNow()
    syncFromEngine()
  }

  async function pushLocal(): Promise<Map<string, Set<number>>> {
    const result = await engine.pushLocal()
    syncFromEngine()
    return result
  }

  async function pullRemote(pushedInCycle?: Map<string, Set<number>>): Promise<void> {
    await engine.pullRemote(pushedInCycle)
    syncFromEngine()
  }

  async function resolveConflictRetry(queueId: number): Promise<void> {
    await engine.resolveConflict(queueId, 'retry')
    syncFromEngine()
  }

  function resolveConflictDiscard(queueId: number): void {
    // discard 分支同步完成 DB 操作；立即对齐缓存，后续微任务再兜底一次
    void engine.resolveConflict(queueId, 'discard').then(syncFromEngine)
    syncFromEngine()
  }

  function markSynced(queueId: number): void {
    // 兼容旧调用方；实际逻辑在 SyncEngine 内部
    void queueId
    syncFromEngine()
  }

  function markConflict(queueId: number): void {
    // 兼容旧调用方；实际逻辑在 SyncEngine 内部
    void queueId
    syncFromEngine()
  }

  function reset(): void {
    engine.reset()
    syncFromEngine()
  }

  return {
    queue,
    syncing,
    lastSyncAt,
    pendingCount,
    activePendingCount,
    conflicts,
    pushErrors,
    enqueue,
    markSynced,
    markConflict,
    resolveConflictRetry,
    resolveConflictDiscard,
    pushLocal,
    pullRemote,
    syncNow,
    init,
    subscribeSyncEngine,
    reset,
  }
})
