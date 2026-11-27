import { describe, it, expect, beforeEach, vi } from 'vitest'
import { initLocalDb, query, run, rowsFrom, resetLocalDb } from '@/db/local'
import { SyncEngine } from '@/sync/SyncEngine'

vi.mock('@/api/daily_expenses', () => ({
  dailyExpensesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() },
}))
vi.mock('@/api/transactions', () => ({
  transactionsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() },
}))
vi.mock('@/api/assets', () => ({
  assetsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() },
}))
vi.mock('@/api/accounts', () => ({
  accountsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() },
}))
vi.mock('@/api/categories', () => ({
  categoriesApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() },
}))
vi.mock('@/api/tags', () => ({
  tagsApi: { create: vi.fn(), update: vi.fn(), delete: vi.fn(), list: vi.fn() },
}))

import { dailyExpensesApi } from '@/api/daily_expenses'
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import { accountsApi } from '@/api/accounts'
import { categoriesApi } from '@/api/categories'
import { tagsApi } from '@/api/tags'

const COMPLETE_EXPENSE = {
  category_id: 1,
  asset_id: 1,
  expense_type: 'expense',
  amount: 42,
  currency: 'CNY',
  expense_date: '2026-11-27',
}

function deferred<T>() {
  let resolve!: (value: T) => void
  let reject!: (reason?: unknown) => void
  const promise = new Promise<T>((res, rej) => {
    resolve = res
    reject = rej
  })
  return { promise, resolve, reject }
}

function mockEmptyLists() {
  ;(accountsApi.list as any).mockResolvedValue([])
  ;(categoriesApi.list as any).mockResolvedValue([])
  ;(tagsApi.list as any).mockResolvedValue([])
  ;(assetsApi.list as any).mockResolvedValue({ list: [], total: 0 })
  ;(transactionsApi.list as any).mockResolvedValue({ list: [], total: 0 })
  ;(dailyExpensesApi.list as any).mockResolvedValue({ list: [], total: 0 })
}

beforeEach(async () => {
  vi.resetAllMocks()
  localStorage.clear()
  resetLocalDb()
  await initLocalDb()
  const engine = SyncEngine.getInstance()
  engine.reset()
  await initLocalDb()
  engine.init()
  engine.clearPushErrors()
  engine.setAuthFailHandler(null)
})

describe('SyncEngine epoch guard, last_sync_at preservation, and 1001 deduplication', () => {
  it('writes zero rows to local SQLite when sync.reset() is called while pullRemote() is awaiting an API promise', async () => {
    const engine = SyncEngine.getInstance()
    const pendingAccounts = deferred<any[]>()
    mockEmptyLists()
    ;(accountsApi.list as any).mockReturnValueOnce(pendingAccounts.promise)

    const pullPromise = engine.pullRemote()
    // Wait a microtask so pullRemote reaches await accountsApi.list(...)
    await Promise.resolve()

    engine.reset()
    // Simulate a fresh session opening a clean local DB before the stale request resolves
    await initLocalDb()
    engine.init()

    pendingAccounts.resolve([
      {
        id: 777,
        name: 'Stale Account',
        institution: 'bank_card',
        archived: 0,
        updated_at: '2026-11-27T05:00:00Z',
      },
    ])
    await pullPromise

    const accountRows = rowsFrom(query('SELECT * FROM accounts'))
    expect(accountRows).toHaveLength(0)
    expect(engine.getState().lastSyncAt).toBeNull()
  })

  it('does not mark new session queued item synced when sync.reset() is called while pushLocal() is awaiting', async () => {
    const engine = SyncEngine.getInstance()
    const pendingCreate = deferred<{ id: number }>()
    ;(dailyExpensesApi.create as any).mockReturnValueOnce(pendingCreate.promise)

    engine.enqueue('daily_expenses', 1001, 'create', COMPLETE_EXPENSE)
    const pushPromise = engine.pushLocal()
    await Promise.resolve()

    engine.reset()
    await initLocalDb()
    engine.init()
    // New session enqueues an item that also receives sync_queue.id = 1
    engine.enqueue('daily_expenses', 1002, 'create', COMPLETE_EXPENSE)

    pendingCreate.resolve({ id: 2001 })
    await pushPromise

    const queueRows = rowsFrom(query('SELECT id, record_id, synced FROM sync_queue'))
    expect(queueRows).toHaveLength(1)
    expect(Number(queueRows[0]?.record_id)).toBe(1002)
    expect(Number(queueRows[0]?.synced)).toBe(0)
    expect(engine.getState().queue).toHaveLength(1)
  })

  it('preserves last_sync_at without updating it when pullRemote fails on one table', async () => {
    const engine = SyncEngine.getInstance()
    const previousSync = '2026-11-25T10:00:00.000Z'
    run("INSERT OR REPLACE INTO sync_meta (key, value) VALUES ('last_sync_at', ?)", [previousSync])
    engine.init()

    mockEmptyLists()
    ;(categoriesApi.list as any).mockRejectedValueOnce(new Error('network failure'))

    await engine.syncNow()

    expect(engine.getState().lastSyncAt).toBe(previousSync)
    const metaRows = rowsFrom(query("SELECT value FROM sync_meta WHERE key = 'last_sync_at'"))
    expect(metaRows[0]?.value).toBe(previousSync)
  })

  it('rethrows 1001 immediately in pullRemote and calls authFailHandler once in syncNow', async () => {
    const engine = SyncEngine.getInstance()
    const onAuthFail = vi.fn()
    engine.setAuthFailHandler(onAuthFail)

    mockEmptyLists()
    ;(categoriesApi.list as any).mockRejectedValueOnce({
      response: { data: { code: 1001, message: 'unauthorized' } },
    })

    await engine.syncNow()

    expect(onAuthFail).toHaveBeenCalledTimes(1)
    expect(tagsApi.list).not.toHaveBeenCalled()
    expect(engine.getState().lastSyncAt).toBeNull()
  })

  it('calls authFailHandler exactly once on 1001 auth fail during pushLocal in syncNow', async () => {
    const engine = SyncEngine.getInstance()
    const onAuthFail = vi.fn()
    engine.setAuthFailHandler(onAuthFail)

    ;(dailyExpensesApi.create as any).mockRejectedValueOnce({
      response: { data: { code: 1001, message: '登录失效' } },
    })
    engine.enqueue('daily_expenses', 301, 'create', COMPLETE_EXPENSE)

    await engine.syncNow()

    expect(onAuthFail).toHaveBeenCalledTimes(1)
    expect(engine.getState().queue).toHaveLength(1)
  })
})
